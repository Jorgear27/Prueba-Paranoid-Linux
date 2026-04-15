package tests

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"backer/internal/handlers"
	"backer/internal/middleware"

	"github.com/gorilla/mux"
)

// predictRouter sets up a minimal router with JWT auth + the predict handler.
func predictRouter(predictorURL string, timeout time.Duration) *mux.Router {
	r := mux.NewRouter()
	r.Use(middleware.AuthMiddleware(testSecret))
	r.HandleFunc("/predict", handlers.Predict(predictorURL, timeout)).Methods("GET")
	return r
}

// predictRequest builds and executes a GET /predict with valid JWT and query params.
func predictRequest(router http.Handler, queryString string) *httptest.ResponseRecorder {
	req := httptest.NewRequest("GET", "/predict?"+queryString, nil)
	req.Header.Set("Authorization", "Bearer "+validToken())
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)
	return w
}

// ---------------------------------------------------------------------------
// Fallback activation — unreachable predictor
// ---------------------------------------------------------------------------

func TestPredict_FallbackOnUnreachablePredictor(t *testing.T) {
	// Point to an address that will refuse connections
	r := predictRouter("http://127.0.0.1:1", 200*time.Millisecond)

	w := predictRequest(r, "origin_id=H001&destination_id=W001&weight_kg=10&total_qty=15")

	if w.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d — body: %s", w.Code, w.Body.String())
	}

	var resp map[string]any
	json.Unmarshal(w.Body.Bytes(), &resp)

	if resp["fallback"] != true {
		t.Error("expected fallback=true when predictor is unreachable")
	}
}

// ---------------------------------------------------------------------------
// Box size selection by total_qty — table driven (spec: ≤20→S, ≤50→M, >50→L)
// ---------------------------------------------------------------------------

func TestPredict_FallbackBoxSize(t *testing.T) {
	cases := []struct {
		name     string
		totalQty string
		wantBox  string
	}{
		{"small_qty_10", "10", "S"},
		{"medium_qty_35", "35", "M"},
		{"large_qty_100", "100", "L"},
	}

	r := predictRouter("http://127.0.0.1:1", 200*time.Millisecond)

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			qs := "origin_id=H001&destination_id=W001&weight_kg=5&total_qty=" + tc.totalQty
			w := predictRequest(r, qs)

			if w.Code != http.StatusOK {
				t.Fatalf("expected 200, got %d", w.Code)
			}

			var resp map[string]any
			json.Unmarshal(w.Body.Bytes(), &resp)

			if resp["box_size"] != tc.wantBox {
				t.Errorf("total_qty=%s: expected box_size=%s, got %v", tc.totalQty, tc.wantBox, resp["box_size"])
			}
			if resp["fallback"] != true {
				t.Error("expected fallback=true")
			}
		})
	}
}

// ---------------------------------------------------------------------------
// Predictor reachable — proxied response
// ---------------------------------------------------------------------------

func TestPredict_ProxiesToPredictor(t *testing.T) {
	// Spin up a fake ML predictor that returns a fixed response
	fakePredictor := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]any{
			"eta_minutes":   42,
			"cost_estimate": 15.5,
			"box_size":      "M",
			"fallback":      false,
		})
	}))
	defer fakePredictor.Close()

	r := predictRouter(fakePredictor.URL, 2*time.Second)
	w := predictRequest(r, "origin_id=H001&destination_id=W001&weight_kg=10")

	if w.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", w.Code)
	}

	var resp map[string]any
	json.Unmarshal(w.Body.Bytes(), &resp)

	if resp["fallback"] == true {
		t.Error("expected fallback=false when predictor is reachable")
	}
	if resp["eta_minutes"] != float64(42) {
		t.Errorf("expected eta_minutes=42, got %v", resp["eta_minutes"])
	}
}
