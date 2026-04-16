package handlers

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"time"

	"backer/internal/bridge"
	"backer/internal/mq"

	"github.com/gorilla/mux"
)

// createShipmentRequest is the expected body for POST /shipments.
type createShipmentRequest struct {
	OriginID      string     `json:"origin_id"`
	DestinationID string     `json:"destination_id"`
	Items         []shipItem `json:"items"`
}

// shipItem represents a single item in a shipment request.
type shipItem struct {
	ItemType int `json:"item_type"`
	Quantity int `json:"quantity"`
}

// createShipmentResponse is the body returned on successful shipment creation.
type createShipmentResponse struct {
	ShipmentID string   `json:"shipment_id"`
	Status     string   `json:"status"`
	Stops      []string `json:"stops"`
}

// dispatchRequest is the expected body for POST /dispatch.
type dispatchRequest struct {
	ShipmentID string   `json:"shipment_id"`
	Status     string   `json:"status"` // "Shipped" or "Canceled"
	EmployeeID string   `json:"employee_id"`
	Stops      []string `json:"stops"`
}

// generateShipmentID produces a simple shipment ID based on timestamp.
// In production this would come from the C++ core or a UUID library.
func generateShipmentID() string {
	return fmt.Sprintf("SHP%d", time.Now().UnixMilli())
}

// CreateShipment handles POST /shipments.
// Validates the request body, forwards it to the C++ core via TCP bridge,
// and on success publishes a ShipmentEvent to RabbitMQ asynchronously.
func CreateShipment(cpp bridge.Bridge, mqProd mq.Publisher) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// Decode and validate request body
		var req createShipmentRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, `{"error":"invalid JSON body"}`, http.StatusBadRequest)
			return
		}
		if req.OriginID == "" {
			http.Error(w, `{"error":"origin_id is required"}`, http.StatusBadRequest)
			return
		}
		if req.DestinationID == "" {
			http.Error(w, `{"error":"destination_id is required"}`, http.StatusBadRequest)
			return
		}
		if len(req.Items) == 0 {
			http.Error(w, `{"error":"items cannot be empty"}`, http.StatusBadRequest)
			return
		}

		shipmentID := generateShipmentID()

		// Build the OrderRequest that the C++ RequestRouter expects
		items := make([]bridge.OrderItem, len(req.Items))
		for i, it := range req.Items {
			items[i] = bridge.OrderItem{
				ItemType: it.ItemType,
				Quantity: it.Quantity,
			}
		}

		cppReq := bridge.OrderRequest{
			Type:    "order_request",
			HubID:   req.OriginID,
			OrderID: shipmentID,
			Items:   items,
		}

		// Forward to C++ core — returns 503 if core is unreachable
		ctx, cancel := context.WithTimeout(r.Context(), 10*time.Second)
		defer cancel()

		resp, err := cpp.SendRequest(ctx, cppReq)
		if err != nil {
			log.Printf("[ERROR] CreateShipment: bridge error: %v", err)
			http.Error(w, `{"error":"core service unavailable"}`, http.StatusServiceUnavailable)
			return
		}

		// Only publish to RabbitMQ after a successful C++ response
		if status, ok := resp["status"].(string); !ok || status != "success" {
			log.Printf("[WARN] CreateShipment: unexpected core response: %v", resp)
			http.Error(w, `{"error":"core rejected the request"}`, http.StatusBadGateway)
			return
		}

		// Extract stops (delivery route) from C++ response for TP4 courier
		stops := []string{}
		if stopsInterface, ok := resp["stops"]; ok {
			if stopsArray, ok := stopsInterface.([]interface{}); ok {
				for _, stop := range stopsArray {
					if stopStr, ok := stop.(string); ok {
						stops = append(stops, stopStr)
					}
				}
			}
		}

		// Publish asynchronously so broker latency never blocks the HTTP response
		go func() {
			event := mq.ShipmentEvent{
				ShipmentID: shipmentID,
				HubID:      req.OriginID,
				Stops:      stops,
				Items:      toMQItems(req.Items),
				Timestamp:  time.Now(),
			}
			if err := mqProd.Publish(context.Background(), event); err != nil {
				log.Printf("[ERROR] CreateShipment: RabbitMQ publish failed: %v", err)
			}
		}()

		// Respond 201 Created with shipment info and computed stops
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusCreated)
		json.NewEncoder(w).Encode(createShipmentResponse{
			ShipmentID: shipmentID,
			Status:     "pending",
			Stops:      stops,
		})
	}
}

// GetStatus handles GET /status/{id}.
// Proxies the status query to the C++ core and returns the response verbatim.
func GetStatus(cpp bridge.Bridge) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		orderID := mux.Vars(r)["id"]
		if orderID == "" {
			http.Error(w, `{"error":"shipment id is required"}`, http.StatusBadRequest)
			return
		}

		// Build the StatusQuery the C++ RequestRouter expects
		query := bridge.StatusQuery{
			Type:      "order_status",
			HubID:     "backer",
			OrderID:   orderID,
			Timestamp: time.Now().UTC().Format(time.RFC3339),
		}

		ctx, cancel := context.WithTimeout(r.Context(), 10*time.Second)
		defer cancel()

		resp, err := cpp.SendRequest(ctx, query)
		if err != nil {
			log.Printf("[ERROR] GetStatus: bridge error: %v", err)
			http.Error(w, `{"error":"core service unavailable"}`, http.StatusServiceUnavailable)
			return
		}

		// Return 404 if the C++ core reports the order was not found
		if status, ok := resp["status"].(string); ok && status == "error" {
			http.Error(w, `{"error":"shipment not found"}`, http.StatusNotFound)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(resp)
	}
}

// Dispatch handles POST /dispatch.
// Routes to order_dispatch (Shipped) or cancel_order (Canceled) on the C++ core.
// When dispatching with status "Shipped", also publishes the route to the courier via MQTT.
func Dispatch(cpp bridge.Bridge, routePublisher *mq.RoutePublisher) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req dispatchRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, `{"error":"invalid JSON body"}`, http.StatusBadRequest)
			return
		}
		if req.ShipmentID == "" {
			http.Error(w, `{"error":"shipment_id is required"}`, http.StatusBadRequest)
			return
		}
		if req.Status != "Shipped" && req.Status != "Canceled" {
			http.Error(w, `{"error":"status must be Shipped or Canceled"}`, http.StatusBadRequest)
			return
		}

		// Only require EmployeeID and Stops for "Shipped" status (dispatch to courier)
		if req.Status == "Shipped" {
			if req.EmployeeID == "" {
				http.Error(w, `{"error":"employee_id is required for Shipped status"}`, http.StatusBadRequest)
				return
			}
			if len(req.Stops) == 0 {
				http.Error(w, `{"error":"stops cannot be empty for Shipped status"}`, http.StatusBadRequest)
				return
			}
		}

		ctx, cancel := context.WithTimeout(r.Context(), 10*time.Second)
		defer cancel()

		statusResp, err := cpp.SendRequest(ctx, bridge.StatusQuery{
			Type:      "order_status",
			HubID:     "backer",
			OrderID:   req.ShipmentID,
			Timestamp: time.Now().UTC().Format(time.RFC3339),
		})
		if err != nil {
			log.Printf("[ERROR] Dispatch: status precheck bridge error: %v", err)
			http.Error(w, `{"error":"core service unavailable"}`, http.StatusServiceUnavailable)
			return
		}
		if status, ok := statusResp["status"].(string); ok {
			switch status {
			case "error":
				http.Error(w, `{"error":"shipment not found"}`, http.StatusNotFound)
				return
			case "Canceled", "Delivered", "Shipped":
				http.Error(w, `{"error":"shipment already finalized"}`, http.StatusConflict)
				return
			}
		}

		var cppReq any

		if req.Status == "Canceled" {
			// cancel_order → OrderManager::handleCancelation
			cppReq = bridge.CancelMsg{
				Type:    "cancel_order",
				OrderID: req.ShipmentID,
			}
		} else {
			// order_dispatch → OrderManager::handleOrderDispatch
			cppReq = bridge.DispatchMsg{
				Type:         "order_dispatch",
				OrderID:      req.ShipmentID,
				Status:       "Shipped",
				ItemsShipped: []bridge.OrderItem{},
			}
		}

		resp, err := cpp.SendRequest(ctx, cppReq)
		if err != nil {
			log.Printf("[ERROR] Dispatch: bridge error: %v", err)
			http.Error(w, `{"error":"core service unavailable"}`, http.StatusServiceUnavailable)
			return
		}

		// Return 409 if the C++ core reports the order is already dispatched or cancelled
		if status, ok := resp["status"].(string); ok && status == "error" {
			http.Error(w, `{"error":"shipment already dispatched or cancelled"}`, http.StatusConflict)
			return
		}

		// Publish route to courier asynchronously if status is "Shipped"
		if req.Status == "Shipped" && routePublisher != nil {
			go func() {
				fullRoute, err := routePublisher.AppendStops(req.EmployeeID, req.Stops)
				if err != nil {
					log.Printf("[WARN] Dispatch: failed to append route for courier %s: %v", req.EmployeeID, err)
					return
				}
				if err := routePublisher.RegisterShipmentStops(req.EmployeeID, req.ShipmentID, req.Stops); err != nil {
					log.Printf("[WARN] Dispatch: failed to track shipment %s for courier %s: %v", req.ShipmentID, req.EmployeeID, err)
				}

				publishCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
				defer cancel()
				if err := routePublisher.PublishRoute(publishCtx, req.EmployeeID, fullRoute); err != nil {
					log.Printf("[WARN] Dispatch: failed to publish route to courier %s: %v", req.EmployeeID, err)
				}
			}()
		} else if req.Status == "Shipped" {
			log.Printf("[WARN] Dispatch: route publisher not configured, skipping route publish for courier %s", req.EmployeeID)
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{"status": "dispatched"})
	}
}

// toMQItems converts handler-level shipItems to mq.Items for the RabbitMQ event.
func toMQItems(items []shipItem) []mq.Item {
	out := make([]mq.Item, len(items))
	for i, it := range items {
		out[i] = mq.Item{ItemType: it.ItemType, Quantity: it.Quantity}
	}
	return out
}
