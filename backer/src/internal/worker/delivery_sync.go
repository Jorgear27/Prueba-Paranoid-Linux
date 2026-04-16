package worker

import (
	"backer/internal/bridge"
	"context"
	"encoding/json"
	"log"
	"strings"
	"time"

	"backer/internal/mq"

	amqp "github.com/rabbitmq/amqp091-go"
)

const deliverySyncQueueName = "backer.delivery_sync"

type deliveredMessage struct {
	Stop   string `json:"stop"`
	Status string `json:"status"`
}

// DeliverySyncWorker consumes delivered.* events and removes completed stops
// from the in-memory employee route queue.
type DeliverySyncWorker struct {
	cancel func()
	done   chan struct{}
}

func StartDeliverySyncWorker(rabbitURL string, routePublisher *mq.RoutePublisher, cpp bridge.Bridge) (*DeliverySyncWorker, error) {
	if routePublisher == nil {
		return nil, nil
	}

	ctx, cancel := context.WithCancel(context.Background())
	w := &DeliverySyncWorker{cancel: cancel, done: make(chan struct{})}

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
		"amq.topic",
		"topic",
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
		deliverySyncQueueName,
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

	if err := ch.QueueBind(queue.Name, "delivered.*", "amq.topic", false, nil); err != nil {
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
		defer close(w.done)
		defer ch.Close()
		defer conn.Close()

		log.Printf("[INFO] DeliverySync: consuming queue=%s binding=delivered.*", queue.Name)

		for {
			select {
			case <-ctx.Done():
				return
			case msg, ok := <-msgs:
				if !ok {
					return
				}
				handleDeliveredEvent(msg, routePublisher, cpp)
			}
		}
	}()

	return w, nil
}

func handleDeliveredEvent(msg amqp.Delivery, routePublisher *mq.RoutePublisher, cpp bridge.Bridge) {
	employeeID := employeeFromRoutingKey(msg.RoutingKey)
	if employeeID == "" {
		log.Printf("[WARN] DeliverySync: invalid routing key=%q", msg.RoutingKey)
		_ = msg.Ack(false)
		return
	}

	var payload deliveredMessage
	if err := json.Unmarshal(msg.Body, &payload); err != nil {
		log.Printf("[WARN] DeliverySync: invalid payload routing_key=%s err=%v", msg.RoutingKey, err)
		_ = msg.Ack(false)
		return
	}

	if payload.Stop == "" {
		_ = msg.Ack(false)
		return
	}

	if payload.Status != "" && !strings.EqualFold(payload.Status, "done") {
		_ = msg.Ack(false)
		return
	}

	shipmentID, err := routePublisher.CompleteShipmentByStop(employeeID, payload.Stop)
	if err != nil {
		log.Printf("[WARN] DeliverySync: complete shipment failed employee=%s stop=%q err=%v", employeeID, payload.Stop, err)
	}
	if shipmentID != "" && cpp != nil {
		deliveryCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		_, sendErr := cpp.SendRequest(deliveryCtx, bridge.DeliveryUpdateMsg{
			Type:      "delivery_update",
			Timestamp: time.Now().UTC().Format(time.RFC3339),
			HubID:     "backer",
			OrderID:   shipmentID,
			Status:    "Delivered",
		})
		cancel()
		if sendErr != nil {
			log.Printf("[WARN] DeliverySync: delivery update failed shipment=%s err=%v", shipmentID, sendErr)
		}
	}

	updatedRoute, removed, err := routePublisher.RemoveStop(employeeID, payload.Stop)
	if err != nil {
		log.Printf("[WARN] DeliverySync: remove stop failed employee=%s stop=%q err=%v", employeeID, payload.Stop, err)
		_ = msg.Ack(false)
		return
	}

	if !removed {
		_ = msg.Ack(false)
		return
	}

	if len(updatedRoute) > 0 {
		publishCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		if err := routePublisher.PublishRoute(publishCtx, employeeID, updatedRoute); err != nil {
			log.Printf("[WARN] DeliverySync: republish route failed employee=%s err=%v", employeeID, err)
		}
	}

	_ = msg.Ack(false)
}

func employeeFromRoutingKey(routingKey string) string {
	if routingKey == "" {
		return ""
	}

	parts := strings.Split(routingKey, ".")
	if len(parts) != 2 || parts[0] != "delivered" {
		return ""
	}

	return strings.TrimSpace(parts[1])
}

func (w *DeliverySyncWorker) Stop() {
	if w == nil {
		return
	}
	w.cancel()
	<-w.done
}
