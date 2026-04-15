package tests

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"backer/internal/bridge"
	"backer/internal/handlers"
	"backer/internal/middleware"
	"backer/internal/mq"

	"github.com/gorilla/mux"
)

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

// fakeBridge implements bridge.Bridge without a real TCP connection.
type fakeBridge struct {
	response map[string]any
	err      error
	lastSent any // captures the last payload for assertions
}

func (f *fakeBridge) SendRequest(_ context.Context, payload any) (map[string]any, error) {
	f.lastSent = payload
	return f.response, f.err
}

func (f *fakeBridge) HealthStatus() string { return "ok" }

// fakePublisher implements mq.Publisher without a real AMQP connection.
type fakePublisher struct {
	events []mq.ShipmentEvent
}

func (f *fakePublisher) Publish(_ context.Context, event mq.ShipmentEvent) error {
	f.events = append(f.events, event)
	return nil
}
func (f *fakePublisher) HealthStatus() string { return "ok" }
func (f *fakePublisher) Close()               {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const testSecret = "test-secret-key"

// authedRouter creates a mux.Router with JWT auth middleware and registers routes.
func authedRouter(cpp bridge.Bridge, pub mq.Publisher) *mux.Router {
	r := mux.NewRouter()
	r.Use(middleware.AuthMiddleware(testSecret))
	r.HandleFunc("/shipments", handlers.CreateShipment(cpp, pub)).Methods("POST")
	r.HandleFunc("/status/{id}", handlers.GetStatus(cpp)).Methods("GET")
	r.HandleFunc("/dispatch", handlers.Dispatch(cpp, nil)).Methods("POST")
	return r
}

// doRequest builds and executes a request with a valid JWT.
func doRequest(router http.Handler, method, path string, body []byte) *httptest.ResponseRecorder {
	var req *http.Request
	if body != nil {
		req = httptest.NewRequest(method, path, bytes.NewReader(body))
	} else {
		req = httptest.NewRequest(method, path, nil)
	}
	req.Header.Set("Authorization", "Bearer "+validToken())
	req.Header.Set("Content-Type", "application/json")

	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)
	return w
}

// ---------------------------------------------------------------------------
// POST /shipments
// ---------------------------------------------------------------------------

func TestCreateShipment_MissingOriginID(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"destination_id":"D1","items":[{"item_type":0,"quantity":10}]}`
	w := doRequest(r, "POST", "/shipments", []byte(body))

	if w.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", w.Code)
	}
}

func TestCreateShipment_EmptyItems(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"origin_id":"H001","destination_id":"D1","items":[]}`
	w := doRequest(r, "POST", "/shipments", []byte(body))

	if w.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", w.Code)
	}
}

func TestCreateShipment_MalformedJSON(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	w := doRequest(r, "POST", "/shipments", []byte(`{not json`))

	if w.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", w.Code)
	}
}

func TestCreateShipment_ValidBody_Returns201(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"origin_id":"H001","destination_id":"W001","items":[{"item_type":0,"quantity":5}]}`
	w := doRequest(r, "POST", "/shipments", []byte(body))

	if w.Code != http.StatusCreated {
		t.Fatalf("expected 201, got %d — body: %s", w.Code, w.Body.String())
	}

	var resp map[string]any
	json.Unmarshal(w.Body.Bytes(), &resp)

	if _, ok := resp["shipment_id"]; !ok {
		t.Error("response missing shipment_id")
	}
	if resp["status"] != "pending" {
		t.Errorf("expected status=pending, got %v", resp["status"])
	}
}

func TestCreateShipment_ValidBody_IncludesStopsFromCore(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{
		"status": "success",
		"stops":  []any{"warehouse1", "warehouse2"},
	}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"origin_id":"H001","destination_id":"W001","items":[{"item_type":0,"quantity":5}]}`
	w := doRequest(r, "POST", "/shipments", []byte(body))

	if w.Code != http.StatusCreated {
		t.Fatalf("expected 201, got %d — body: %s", w.Code, w.Body.String())
	}

	var resp map[string]any
	json.Unmarshal(w.Body.Bytes(), &resp)

	stops, ok := resp["stops"].([]any)
	if !ok {
		t.Fatalf("expected stops array in response, got: %T", resp["stops"])
	}
	if len(stops) != 2 {
		t.Fatalf("expected 2 stops, got %d", len(stops))
	}
	if stops[0] != "warehouse1" || stops[1] != "warehouse2" {
		t.Fatalf("unexpected stops: %v", stops)
	}
}

func TestCreateShipment_BridgeDown_Returns503(t *testing.T) {
	cpp := &fakeBridge{err: context.DeadlineExceeded}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"origin_id":"H001","destination_id":"W001","items":[{"item_type":1,"quantity":3}]}`
	w := doRequest(r, "POST", "/shipments", []byte(body))

	if w.Code != http.StatusServiceUnavailable {
		t.Errorf("expected 503, got %d", w.Code)
	}
}

// ---------------------------------------------------------------------------
// POST /dispatch
// ---------------------------------------------------------------------------

func TestDispatch_InvalidStatus(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"shipment_id":"SHP001","status":"Unknown"}`
	w := doRequest(r, "POST", "/dispatch", []byte(body))

	if w.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", w.Code)
	}
}

func TestDispatch_Delivered_OK(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"shipment_id":"SHP001","status":"Delivered","employee_id":"E001","stops":["warehouse1","warehouse2"]}`
	w := doRequest(r, "POST", "/dispatch", []byte(body))

	if w.Code != http.StatusOK {
		t.Errorf("expected 200, got %d — body: %s", w.Code, w.Body.String())
	}
}

func TestDispatch_Delivered_MissingEmployeeID_Returns400(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"shipment_id":"SHP001","status":"Delivered","stops":["warehouse1"]}`
	w := doRequest(r, "POST", "/dispatch", []byte(body))

	if w.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d — body: %s", w.Code, w.Body.String())
	}
}

func TestDispatch_Delivered_EmptyStops_Returns400(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"shipment_id":"SHP001","status":"Delivered","employee_id":"E001","stops":[]}`
	w := doRequest(r, "POST", "/dispatch", []byte(body))

	if w.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d — body: %s", w.Code, w.Body.String())
	}
}

func TestDispatch_Canceled_OK(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"shipment_id":"SHP001","status":"Canceled"}`
	w := doRequest(r, "POST", "/dispatch", []byte(body))

	if w.Code != http.StatusOK {
		t.Errorf("expected 200, got %d — body: %s", w.Code, w.Body.String())
	}
}

func TestDispatch_AlreadyDispatched_Returns409(t *testing.T) {
	// C++ core returns error when order is already dispatched
	cpp := &fakeBridge{response: map[string]any{"status": "error", "message": "already dispatched"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	body := `{"shipment_id":"SHP001","status":"Delivered","employee_id":"E001","stops":["warehouse1"]}`
	w := doRequest(r, "POST", "/dispatch", []byte(body))

	if w.Code != http.StatusConflict {
		t.Errorf("expected 409, got %d", w.Code)
	}
}

// ---------------------------------------------------------------------------
// GET /status/{id}
// ---------------------------------------------------------------------------

func TestGetStatus_NotFound(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "error"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	w := doRequest(r, "GET", "/status/NONEXISTENT", nil)

	if w.Code != http.StatusNotFound {
		t.Errorf("expected 404, got %d", w.Code)
	}
}

func TestGetStatus_Found(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{
		"status":   "success",
		"order_id": "SHP001",
		"state":    "Shipped",
	}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	w := doRequest(r, "GET", "/status/SHP001", nil)

	if w.Code != http.StatusOK {
		t.Errorf("expected 200, got %d", w.Code)
	}
}

// ---------------------------------------------------------------------------
// Auth rejection
// ---------------------------------------------------------------------------

func TestMissingJWT_Returns401(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	req := httptest.NewRequest("POST", "/shipments", bytes.NewBufferString(`{}`))
	// No Authorization header
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)

	if w.Code != http.StatusUnauthorized {
		t.Errorf("expected 401, got %d", w.Code)
	}
}

func TestInvalidJWT_Returns401(t *testing.T) {
	cpp := &fakeBridge{response: map[string]any{"status": "success"}}
	pub := &fakePublisher{}
	r := authedRouter(cpp, pub)

	req := httptest.NewRequest("POST", "/shipments", bytes.NewBufferString(`{}`))
	req.Header.Set("Authorization", "Bearer totally.invalid.token")
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)

	if w.Code != http.StatusUnauthorized {
		t.Errorf("expected 401, got %d", w.Code)
	}
}
