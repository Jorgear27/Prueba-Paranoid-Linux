package mq

import (
	"context"
	"encoding/json"
	"log"
	"sync"
	"time"

	"backer/internal/metrics"

	amqp "github.com/rabbitmq/amqp091-go"
)

// exchangeName is the durable fanout exchange all downstream services bind to.
// Downstream consumers declare their own queues — the Backer never needs to know queue names.
const exchangeName = "shipments"

// ShipmentEvent is the message published to RabbitMQ after a successful shipment creation.
type ShipmentEvent struct {
	ShipmentID string    `json:"shipment_id"`
	HubID      string    `json:"hub_id"`
	Stops      []string  `json:"stops"`
	Items      []Item    `json:"items"`
	Timestamp  time.Time `json:"timestamp"`
}

// Item represents a single item in a shipment event.
type Item struct {
	ItemType int `json:"item_type"`
	Quantity int `json:"quantity"`
}

// Publisher is the interface used by handlers to publish events.
// Producer is the production implementation; tests can provide fakes.
type Publisher interface {
	Publish(ctx context.Context, event ShipmentEvent) error
	HealthStatus() string
	Close()
}

// Producer holds the AMQP connection and channel.
// On publish failure the channel is torn down and rebuilt on the next call.
type Producer struct {
	mu      sync.Mutex
	url     string
	conn    *amqp.Connection
	channel *amqp.Channel
}

// NewProducer creates a Producer and attempts an initial connection.
// Connection failure at startup is non-fatal — the server continues running.
func NewProducer(url string) *Producer {
	p := &Producer{url: url}
	if err := p.connect(); err != nil {
		log.Printf("[WARN] RabbitMQ: initial connection failed: %v", err)
	}
	return p
}

// connect establishes the AMQP connection and declares the durable fanout exchange.
func (p *Producer) connect() error {
	conn, err := amqp.Dial(p.url)
	if err != nil {
		return err
	}

	ch, err := conn.Channel()
	if err != nil {
		conn.Close()
		return err
	}

	// Declare a durable fanout exchange so messages survive broker restarts
	// and any number of downstream queues can bind without Backer knowing their names.
	if err := ch.ExchangeDeclare(
		exchangeName,
		"fanout",
		true,  // durable
		false, // auto-deleted
		false, // internal
		false, // no-wait
		nil,
	); err != nil {
		ch.Close()
		conn.Close()
		return err
	}

	p.conn = conn
	p.channel = ch
	log.Printf("[INFO] RabbitMQ: connected, exchange %q declared", exchangeName)
	return nil
}

// ensureChannel rebuilds the connection if the channel is nil or closed.
// Uses exponential back-off capped at 30s.
func (p *Producer) ensureChannel(ctx context.Context) error {
	if p.channel != nil && !p.conn.IsClosed() {
		return nil
	}

	delay := time.Second
	for {
		if err := p.connect(); err == nil {
			return nil
		}
		log.Printf("[WARN] RabbitMQ: reconnecting in %s", delay)
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(delay):
		}
		delay *= 2
		if delay > 30*time.Second {
			delay = 30 * time.Second
		}
	}
}

// Publish sends a ShipmentEvent to the shipments exchange.
// Messages are persistent (DeliveryMode=2) so they survive broker restarts.
// Called in a non-blocking goroutine from the shipments handler so the HTTP
// response is never delayed by broker latency.
func (p *Producer) Publish(ctx context.Context, event ShipmentEvent) error {
	if err := p.ensureChannel(ctx); err != nil {
		return err
	}

	body, err := json.Marshal(event)
	if err != nil {
		return err
	}

	err = p.channel.PublishWithContext(
		ctx,
		exchangeName,
		"",    // routing key — ignored by fanout exchanges
		false, // mandatory
		false, // immediate
		amqp.Publishing{
			ContentType:  "application/json",
			DeliveryMode: amqp.Persistent, // survive broker restarts
			Body:         body,
		},
	)
	if err != nil {
		// Tear down the channel so ensureChannel rebuilds it on the next call
		p.channel = nil
		metrics.IncMQPublish("error")
		return err
	}

	metrics.IncMQPublish("ok")
	return nil
}

// HealthStatus returns "ok" if the broker connection is alive, "unreachable" otherwise.
// Called by the /health handler in main.go.
func (p *Producer) HealthStatus() string {
	if p.conn == nil || p.conn.IsClosed() {
		return "unreachable"
	}
	return "ok"
}

// Close gracefully closes the channel and connection on server shutdown.
func (p *Producer) Close() {
	if p.channel != nil {
		p.channel.Close()
	}
	if p.conn != nil {
		p.conn.Close()
	}
}

// teardown forces the connection state closed so the next publish reconnects.
func (p *Producer) teardown() {
	if p.channel != nil {
		p.channel.Close()
		p.channel = nil
	}
	if p.conn != nil {
		p.conn.Close()
		p.conn = nil
	}
}

// EnsureChannel rebuilds the connection if the channel is nil or closed.
// Public wrapper for ensureChannel, used by RoutePublisher.
func (p *Producer) EnsureChannel(ctx context.Context) error {
	return p.ensureChannel(ctx)
}

// PublishToExchange publishes a message to a custom exchange with a routing key.
// Used by RoutePublisher to publish to amq.topic for MQTT bridging.
func (p *Producer) PublishToExchange(ctx context.Context, exchange, routingKey string, msg amqp.Publishing) error {
	p.mu.Lock()
	defer p.mu.Unlock()

	if err := p.ensureChannel(ctx); err != nil {
		return err
	}

	if err := p.channel.PublishWithContext(
		ctx,
		exchange,   // exchange name
		routingKey, // routing key
		false,      // mandatory
		false,      // immediate
		msg,
	); err != nil {
		p.teardown()
		metrics.IncMQPublish("error")
		return err
	}

	metrics.IncMQPublish("ok")
	return nil
}
