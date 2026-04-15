package handlers

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"strconv"
	"time"
)

// predictResponse is returned to the client for both real and fallback responses.
type predictResponse struct {
	ETAMinutes   int     `json:"eta_minutes"`
	CostEstimate float64 `json:"cost_estimate"`
	BoxSize      string  `json:"box_size"`
	Fallback     bool    `json:"fallback"`
}

// Predict handles GET /predict.
// Proxies the request to the ML predictor service.
// If the predictor is unreachable or times out, returns a heuristic fallback
// without blocking or erroring the caller.
func Predict(predictorURL string, timeout time.Duration) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// Parse query params
		originID := r.URL.Query().Get("origin_id")
		destinationID := r.URL.Query().Get("destination_id")
		weightStr := r.URL.Query().Get("weight_kg")

		if originID == "" || destinationID == "" {
			http.Error(w, `{"error":"origin_id and destination_id are required"}`, http.StatusBadRequest)
			return
		}

		weightKg, err := strconv.ParseFloat(weightStr, 64)
		if err != nil || weightKg <= 0 {
			http.Error(w, `{"error":"weight_kg must be a positive number"}`, http.StatusBadRequest)
			return
		}

		totalQty := 0
		if qtyStr := r.URL.Query().Get("total_qty"); qtyStr != "" {
			if q, err := strconv.Atoi(qtyStr); err == nil {
				totalQty = q
			}
		}

		// Try the ML predictor with a timeout
		ctx, cancel := context.WithTimeout(r.Context(), timeout)
		defer cancel()

		result, err := callPredictor(ctx, predictorURL, originID, destinationID, weightKg)
		if err != nil {
			// Predictor is offline or timed out — return graceful fallback
			log.Printf("[WARN] Predict: predictor unavailable, using fallback: %v", err)
			w.Header().Set("Content-Type", "application/json")
			json.NewEncoder(w).Encode(fallbackResponse(totalQty))
			return
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(result)
	}
}

// callPredictor forwards the request to the ML predictor service.
// Returns an error if the service is unreachable or the context deadline is exceeded.
func callPredictor(ctx context.Context, predictorURL, originID, destinationID string, weightKg float64) (*predictResponse, error) {
	reqURL := fmt.Sprintf("%s/predict?origin_id=%s&destination_id=%s&weight_kg=%g",
		predictorURL, originID, destinationID, weightKg)

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, reqURL, nil)
	if err != nil {
		return nil, err
	}

	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	var result predictResponse
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, err
	}
	return &result, nil
}

// fallbackResponse returns a heuristic estimate when the ML predictor is offline.
// Box size is chosen by total quantity, ETA is doubled to signal uncertainty.
// fallback: true signals to the client that this is an estimate, not an ML result.
func fallbackResponse(totalQty int) predictResponse {
	boxSize := "S"
	if totalQty > 20 {
		boxSize = "M"
	}
	if totalQty > 50 {
		boxSize = "L"
	}

	return predictResponse{
		ETAMinutes:   90, // double the typical estimate
		CostEstimate: float64(totalQty) * 2.5,
		BoxSize:      boxSize,
		Fallback:     true,
	}
}
