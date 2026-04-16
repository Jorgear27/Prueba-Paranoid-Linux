package worker

import (
	"context"
	"encoding/json"
	"log"
	"time"

	"backer/internal/bridge"
	"backer/internal/mq"

	amqp "github.com/rabbitmq/amqp091-go"
)

const autoDispatchQueueName = "backer.auto_dispatch"

// AutoDispatcher consumes ShipmentEvent messages and dispatches orders automatically.
type AutoDispatcher struct {
	cancel func()
	done   chan struct{}
}

// StartAutoDispatcher starts the RabbitMQ consumer loop.
// The worker is non-HTTP and runs for the process lifetime until Stop is called.
func StartAutoDispatcher(
	rabbitURL string,
	cpp bridge.Bridge,
	routePublisher *mq.RoutePublisher,
	employeeID string,
	dispatchDelay time.Duration,
) (*AutoDispatcher, error) {
	ctx, cancel := context.WithCancel(context.Background())
	d := &AutoDispatcher{cancel: cancel, done: make(chan struct{})}

	conn, err := amqp.Dial(rabbitURL)
	if err != nil {
		cancel()
		return nil, err
	}

	ch, err := conn.Channel()
	if err != nil {
		conn.Close()
		cancel()
		return nil, err
	}

	if err := ch.ExchangeDeclare(
		"shipments",
		"fanout",
		true,
		false,
		false,
		false,
		nil,
	); err != nil {
		ch.Close()
		conn.Close()
		cancel()
		return nil, err
	}

	queue, err := ch.QueueDeclare(
		autoDispatchQueueName,
		true,
		false,
		false,
		false,
		nil,
	)
	if err != nil {
		ch.Close()
		conn.Close()
		cancel()
		return nil, err
	}

	if err := ch.QueueBind(queue.Name, "", "shipments", false, nil); err != nil {
		ch.Close()
		conn.Close()
		cancel()
		return nil, err
	}

	msgs, err := ch.Consume(
		queue.Name,
		"",
		false,
		false,
		false,
		false,
		nil,
	)
	if err != nil {
		ch.Close()
		conn.Close()
		cancel()
		return nil, err
	}

	go func() {
		defer close(d.done)
		defer ch.Close()
		defer conn.Close()

		log.Printf("[INFO] AutoDispatcher: consuming queue=%s employee_id=%s delay=%s", queue.Name, employeeID, dispatchDelay)

		for {
			select {
			case <-ctx.Done():
				return
			case msg, ok := <-msgs:
				if !ok {
					return
				}
				d.handleMessage(ctx, msg, cpp, routePublisher, employeeID, dispatchDelay)
			}
		}
	}()

	return d, nil
}

func (d *AutoDispatcher) handleMessage(
	ctx context.Context,
	msg amqp.Delivery,
	cpp bridge.Bridge,
	routePublisher *mq.RoutePublisher,
	employeeID string,
	dispatchDelay time.Duration,
) {
	var event mq.ShipmentEvent
	if err := json.Unmarshal(msg.Body, &event); err != nil {
		log.Printf("[WARN] AutoDispatcher: invalid event payload: %v", err)
		_ = msg.Ack(false)
		return
	}

	if event.ShipmentID == "" {
		log.Printf("[WARN] AutoDispatcher: missing shipment_id, dropping event")
		_ = msg.Ack(false)
		return
	}

	if len(event.Stops) == 0 {
		log.Printf("[WARN] AutoDispatcher: shipment %s has empty stops, skipping auto-dispatch", event.ShipmentID)
		_ = msg.Ack(false)
		return
	}

	if dispatchDelay > 0 {
		select {
		case <-ctx.Done():
			_ = msg.Nack(false, true)
			return
		case <-time.After(dispatchDelay):
		}
	}

	statusCtx, statusCancel := context.WithTimeout(context.Background(), 10*time.Second)
	statusResp, err := cpp.SendRequest(statusCtx, bridge.StatusQuery{
		Type:      "order_status",
		HubID:     "backer",
		OrderID:   event.ShipmentID,
		Timestamp: time.Now().UTC().Format(time.RFC3339),
	})
	statusCancel()
	if err != nil {
		log.Printf("[WARN] AutoDispatcher: status precheck bridge error for %s: %v", event.ShipmentID, err)
		_ = msg.Nack(false, true)
		return
	}
	if status, ok := statusResp["status"].(string); ok {
		if status == "Canceled" || status == "Delivered" || status == "Shipped" || status == "error" {
			log.Printf("[INFO] AutoDispatcher: skipping shipment %s due to current status=%s", event.ShipmentID, status)
			_ = msg.Ack(false)
			return
		}
	}

	cppReq := bridge.DispatchMsg{
		Type:         "order_dispatch",
		OrderID:      event.ShipmentID,
		Status:       "Shipped",
		ItemsShipped: []bridge.OrderItem{},
	}

	dispatchCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	resp, err := cpp.SendRequest(dispatchCtx, cppReq)
	if err != nil {
		log.Printf("[WARN] AutoDispatcher: dispatch bridge error for %s: %v", event.ShipmentID, err)
		_ = msg.Nack(false, true)
		return
	}

	if status, ok := resp["status"].(string); ok && status == "error" {
		// Usually means already canceled/dispatched; do not retry forever.
		log.Printf("[INFO] AutoDispatcher: skipping shipment %s due to core status=error", event.ShipmentID)
		_ = msg.Ack(false)
		return
	}

	if routePublisher != nil {
		fullRoute, appendErr := routePublisher.AppendStops(employeeID, event.Stops)
		if appendErr != nil {
			log.Printf("[WARN] AutoDispatcher: failed to append route for courier %s shipment=%s err=%v", employeeID, event.ShipmentID, appendErr)
			_ = msg.Nack(false, true)
			return
		}

		if err := routePublisher.RegisterShipmentStops(employeeID, event.ShipmentID, event.Stops); err != nil {
			log.Printf("[WARN] AutoDispatcher: failed to track shipment stops for courier %s shipment=%s err=%v", employeeID, event.ShipmentID, err)
		}

		published := false
		for attempt := 1; attempt <= 3; attempt++ {
			publishCtx, pubCancel := context.WithTimeout(context.Background(), 5*time.Second)
			err = routePublisher.PublishRoute(publishCtx, employeeID, fullRoute)
			pubCancel()
			if err == nil {
				published = true
				break
			}
			log.Printf("[WARN] AutoDispatcher: route publish failed attempt=%d shipment=%s err=%v", attempt, event.ShipmentID, err)
			time.Sleep(500 * time.Millisecond)
		}
		if !published {
			_ = msg.Nack(false, true)
			return
		}
	}

	log.Printf("[INFO] AutoDispatcher: auto-dispatched shipment %s to courier %s", event.ShipmentID, employeeID)
	_ = msg.Ack(false)
}

// Stop gracefully terminates the worker.
func (d *AutoDispatcher) Stop() {
	if d == nil {
		return
	}
	d.cancel()
	<-d.done
}
