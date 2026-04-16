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
	"sync"

	amqp "github.com/rabbitmq/amqp091-go"
)

// RoutePublisher publishes route lists to the RabbitMQ amq.topic exchange
// so the MQTT plugin forwards them to the courier ESP32 device.
type RoutePublisher struct {
	producer *Producer // reuses the existing connection pool

	mu        sync.Mutex
	routes    map[string][]string
	shipments map[string][]shipmentTrack
}

type shipmentTrack struct {
	shipmentID string
	remaining  map[string]struct{}
}

// NewRoutePublisher wraps an existing Producer to add route publishing.
func NewRoutePublisher(p *Producer) *RoutePublisher {
	return &RoutePublisher{
		producer:  p,
		routes:    make(map[string][]string),
		shipments: make(map[string][]shipmentTrack),
	}
}

// AppendStops appends shipment stops to the in-memory route queue of an employee.
// It returns a full snapshot of the updated route to publish.
func (r *RoutePublisher) AppendStops(employeeID string, stops []string) ([]string, error) {
	if employeeID == "" {
		return nil, fmt.Errorf("route_publisher: employeeID must not be empty")
	}
	if len(stops) == 0 {
		return nil, fmt.Errorf("route_publisher: stops list must not be empty")
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	current := r.routes[employeeID]

	indexByStop := make(map[string]int, len(current))
	for i, stop := range current {
		indexByStop[stop] = i
	}

	for _, rawStop := range stops {
		stop := strings.TrimSpace(rawStop)
		if stop == "" {
			continue
		}

		if idx, found := indexByStop[stop]; found {
			// Stop already exists: move it to the tail so a reassigned warehouse
			// is treated as most recent work for the courier.
			current = append(current[:idx], current[idx+1:]...)
			for s, i := range indexByStop {
				if i > idx {
					indexByStop[s] = i - 1
				}
			}
		}

		current = append(current, stop)
		indexByStop[stop] = len(current) - 1
	}
	r.routes[employeeID] = current

	// Return a copy to keep internal state encapsulated.
	out := make([]string, len(current))
	copy(out, current)
	return out, nil
}

// RemoveStop removes the first matching stop from the employee route queue.
// It returns a snapshot of the updated route and whether a stop was removed.
func (r *RoutePublisher) RemoveStop(employeeID string, stop string) ([]string, bool, error) {
	if employeeID == "" {
		return nil, false, fmt.Errorf("route_publisher: employeeID must not be empty")
	}

	trimmed := strings.TrimSpace(stop)
	if trimmed == "" {
		return nil, false, fmt.Errorf("route_publisher: stop must not be empty")
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	current := r.routes[employeeID]
	idx := -1
	for i, s := range current {
		if s == trimmed {
			idx = i
			break
		}
	}

	if idx == -1 {
		out := make([]string, len(current))
		copy(out, current)
		return out, false, nil
	}

	updated := make([]string, 0, len(current)-1)
	updated = append(updated, current[:idx]...)
	updated = append(updated, current[idx+1:]...)
	r.routes[employeeID] = updated

	out := make([]string, len(updated))
	copy(out, updated)
	return out, true, nil
}

// RegisterShipmentStops tracks which route stops must be completed before a
// shipment can be marked as delivered.
func (r *RoutePublisher) RegisterShipmentStops(employeeID, shipmentID string, stops []string) error {
	if employeeID == "" {
		return fmt.Errorf("route_publisher: employeeID must not be empty")
	}
	if shipmentID == "" {
		return fmt.Errorf("route_publisher: shipmentID must not be empty")
	}

	remaining := make(map[string]struct{})
	for _, rawStop := range stops {
		stop := strings.TrimSpace(rawStop)
		if stop == "" {
			continue
		}
		remaining[stop] = struct{}{}
	}
	if len(remaining) == 0 {
		return fmt.Errorf("route_publisher: stops list must not be empty")
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	queue := r.shipments[employeeID]
	queue = append(queue, shipmentTrack{shipmentID: shipmentID, remaining: remaining})
	r.shipments[employeeID] = queue
	return nil
}

// CompleteShipmentByStop consumes one stop for the first shipment in queue that
// still requires it. It returns a shipment ID only when all required stops for
// that shipment are completed.
func (r *RoutePublisher) CompleteShipmentByStop(employeeID, stop string) (string, error) {
	if employeeID == "" {
		return "", fmt.Errorf("route_publisher: employeeID must not be empty")
	}

	trimmed := strings.TrimSpace(stop)
	if trimmed == "" {
		return "", fmt.Errorf("route_publisher: stop must not be empty")
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	queue := r.shipments[employeeID]
	for i := range queue {
		if _, ok := queue[i].remaining[trimmed]; !ok {
			continue
		}

		delete(queue[i].remaining, trimmed)
		if len(queue[i].remaining) != 0 {
			r.shipments[employeeID] = queue
			return "", nil
		}

		shipmentID := queue[i].shipmentID
		queue = append(queue[:i], queue[i+1:]...)
		r.shipments[employeeID] = queue
		return shipmentID, nil
	}

	return "", nil
}

// PublishRoute sends the ordered stop list to the courier device identified
// by employeeID. The stops slice should contain human-readable place names
// matching the node IDs or aliases in the routing graph.
//
// Example:
//
//	PublishRoute(ctx, "E042", []string{"Mercado Sur", "Mercado Norte", "H001"})
//
// Publishes to AMQP routing key "routes.E042" on exchange "amq.topic".
// The ESP32 receives this as MQTT topic "routes/E042" with payload:
//
//	["Mercado Sur", "Mercado Norte", "H001"]
func (r *RoutePublisher) PublishRoute(ctx context.Context,
	employeeID string,
	stops []string) error {

	if employeeID == "" {
		return fmt.Errorf("route_publisher: employeeID must not be empty")
	}
	if len(stops) == 0 {
		return fmt.Errorf("route_publisher: stops list must not be empty")
	}
	if r.producer == nil {
		return fmt.Errorf("route_publisher: producer not configured")
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
