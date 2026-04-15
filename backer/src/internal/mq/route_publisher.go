// Package mq — route_publisher.go
//
// RoutePublisher sends a JSON array of stop names to the RabbitMQ amq.topic
// exchange with routing key "routes.{employee_id}". The RabbitMQ MQTT plugin
// (rabbitmq_mqtt) maps this automatically to the MQTT topic
// "routes/{employee_id}" which the ESP32 courier device subscribes to.
//
// When to publish:
//   The Backer calls PublishRoute() from the Dispatch handler after the C++
//   server confirms a shipment is on the way. The stop list is provided by
//   the caller via the POST /dispatch request body, typically derived from
//   the C++ server's routing computation. The route is then published to the
//   courier's MQTT topic for TP4 delivery tracking.
//
// RabbitMQ MQTT plugin topic mapping:
//   AMQP routing key: routes.E042
//   MQTT topic:       routes/E042          ← ESP32 subscribes to this
//
// The amq.topic exchange is the default exchange used by the MQTT plugin.
// Publishing to it with the right routing key is equivalent to sending to
// the MQTT topic without needing a separate MQTT client in Go.

package mq

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"strings"

	amqp "github.com/rabbitmq/amqp091-go"
)

// RoutePublisher publishes route lists to the RabbitMQ amq.topic exchange
// so the MQTT plugin forwards them to the courier ESP32 device.
type RoutePublisher struct {
	producer *Producer // reuses the existing connection pool
}

// NewRoutePublisher wraps an existing Producer to add route publishing.
func NewRoutePublisher(p *Producer) *RoutePublisher {
	return &RoutePublisher{producer: p}
}

// PublishRoute sends the ordered stop list to the courier device identified
// by employeeID. The stops slice should contain human-readable place names
// matching the node IDs or aliases in the routing graph.
//
// Example:
//   PublishRoute(ctx, "E042", []string{"Mercado Sur", "Mercado Norte", "H001"})
//
// Publishes to AMQP routing key "routes.E042" on exchange "amq.topic".
// The ESP32 receives this as MQTT topic "routes/E042" with payload:
//   ["Mercado Sur", "Mercado Norte", "H001"]
func (r *RoutePublisher) PublishRoute(ctx context.Context,
	employeeID string,
	stops []string) error {

	if employeeID == "" {
		return fmt.Errorf("route_publisher: employeeID must not be empty")
	}
	if len(stops) == 0 {
		return fmt.Errorf("route_publisher: stops list must not be empty")
	}

	payload, err := json.Marshal(stops)
	if err != nil {
		return fmt.Errorf("route_publisher: marshal stops: %w", err)
	}

	// AMQP routing key uses dots; MQTT plugin converts to slashes.
	// "routes.E042" → MQTT topic "routes/E042"
	routingKey := "routes." + sanitiseEmployeeID(employeeID)

	if err := r.producer.EnsureChannel(ctx); err != nil {
		return fmt.Errorf("route_publisher: channel: %w", err)
	}

	msg := amqp.Publishing{
		ContentType:  "application/json",
		DeliveryMode: amqp.Persistent,
		Body:         payload,
	}

	// Publish to the amq.topic exchange — the MQTT plugin's default exchange.
	// The routing key must use dots as separators (AMQP convention).
	if err := r.producer.PublishToExchange(ctx, "amq.topic", routingKey, msg); err != nil {
		return fmt.Errorf("route_publisher: publish: %w", err)
	}

	log.Printf("route_publisher: sent %d stops to courier %s (key=%s)",
		len(stops), employeeID, routingKey)
	return nil
}

// sanitiseEmployeeID replaces any characters that are invalid in AMQP routing
// keys (slashes, spaces) with underscores so the routing key is always valid.
func sanitiseEmployeeID(id string) string {
	return strings.Map(func(r rune) rune {
		if r == '/' || r == ' ' || r == '\t' {
			return '_'
		}
		return r
	}, id)
}
