package metrics

import (
	"net/http"
	"strconv"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promauto"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

// The five metric families exported by the Backer Server.
// All use promauto so they register themselves on package init
// without manual MustRegister calls.
var (
	// requestsTotal counts HTTP requests by method, path and response status.
	requestsTotal = promauto.NewCounterVec(
		prometheus.CounterOpts{
			Name: "backer_requests_total",
			Help: "Total number of HTTP requests by method, path and status.",
		},
		[]string{"method", "path", "status"},
	)

	// requestDuration tracks HTTP request latency as a histogram.
	requestDuration = promauto.NewHistogramVec(
		prometheus.HistogramOpts{
			Name:    "backer_request_duration_seconds",
			Help:    "HTTP request latency in seconds.",
			Buckets: prometheus.DefBuckets,
		},
		[]string{"method", "path"},
	)

	// activeWebsockets tracks the current number of open WebSocket connections.
	activeWebsockets = promauto.NewGauge(
		prometheus.GaugeOpts{
			Name: "backer_active_websocket_connections",
			Help: "Number of currently active WebSocket connections.",
		},
	)

	// mqPublishesTotal counts RabbitMQ publish attempts by outcome.
	mqPublishesTotal = promauto.NewCounterVec(
		prometheus.CounterOpts{
			Name: "backer_rabbitmq_publishes_total",
			Help: "Total RabbitMQ publish attempts, labelled ok or error.",
		},
		[]string{"status"},
	)

	// cppBridgeRequestsTotal counts TCP bridge requests to the C++ core by outcome.
	cppBridgeRequestsTotal = promauto.NewCounterVec(
		prometheus.CounterOpts{
			Name: "backer_cpp_bridge_requests_total",
			Help: "Total requests sent to the C++ core via TCP bridge, labelled ok or error.",
		},
		[]string{"status"},
	)
)

// Init is called from main.go to ensure the metrics package is initialised.
// With promauto the metrics register themselves, so this is a no-op —
// it exists to make the initialisation intent explicit.
func Init() {}

// IncWebsocket increments the active WebSocket connections gauge.
// Called by the WS hub when a client registers.
func IncWebsocket() {
	activeWebsockets.Inc()
}

// DecWebsocket decrements the active WebSocket connections gauge.
// Called by the WS hub when a client unregisters.
func DecWebsocket() {
	activeWebsockets.Dec()
}

// IncMQPublish records a RabbitMQ publish outcome ("ok" or "error").
func IncMQPublish(status string) {
	mqPublishesTotal.WithLabelValues(status).Inc()
}

// IncCppBridge records a C++ bridge request outcome ("ok" or "error").
func IncCppBridge(status string) {
	cppBridgeRequestsTotal.WithLabelValues(status).Inc()
}

// responseWriter wraps http.ResponseWriter to capture the status code
// written by downstream handlers so the metrics middleware can record it.
type responseWriter struct {
	http.ResponseWriter
	statusCode int
}

func newResponseWriter(w http.ResponseWriter) *responseWriter {
	// Default to 200 in case WriteHeader is never called
	return &responseWriter{w, http.StatusOK}
}

func (rw *responseWriter) WriteHeader(code int) {
	rw.statusCode = code
	rw.ResponseWriter.WriteHeader(code)
}

// Middleware records request count and latency for every HTTP request.
// Wraps the response writer to capture the status code written by handlers.
func Middleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		rw := newResponseWriter(w)

		next.ServeHTTP(rw, r)

		duration := time.Since(start).Seconds()
		status := strconv.Itoa(rw.statusCode)

		requestsTotal.WithLabelValues(r.Method, r.URL.Path, status).Inc()
		requestDuration.WithLabelValues(r.Method, r.URL.Path).Observe(duration)
	})
}

// HTTPHandler returns the Prometheus metrics HTTP handler for GET /metrics.
func HTTPHandler() http.HandlerFunc {
	return promhttp.Handler().ServeHTTP
}
