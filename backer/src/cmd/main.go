package main

import (
	"context"
	"encoding/json"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"

	"backer/internal/bridge"
	"backer/internal/handlers"
	"backer/internal/metrics"
	"backer/internal/middleware"
	"backer/internal/mq"
	"backer/internal/registry"
	"backer/internal/worker"

	"github.com/gorilla/mux"
)

func main() {
	// Config desde env vars
	port := getEnv("BACKER_PORT", "9090")
	cppAddr := getEnv("CPP_TCP_ADDR", "localhost:8080")
	rabbitURL := getEnv("RABBITMQ_URL", "amqp://guest:guest@localhost:5672/")
	jwtSecret := getEnv("JWT_SECRET", "")
	rateLimitRPS := getEnvInt("RATE_LIMIT_RPS", 100)
	eurekaURL := getEnv("EUREKA_URL", "http://localhost:8761/eureka")
	predictorURL := getEnv("PREDICTOR_URL", "http://localhost:8082")
	predictorTimeout := getEnvInt("PREDICTOR_TIMEOUT_MS", 500)
	autoDispatchEnabled := getEnvBool("AUTO_DISPATCH_ENABLED", false)
	autoDispatchEmployeeID := getEnv("AUTO_DISPATCH_EMPLOYEE_ID", "E001")
	autoDispatchDelayMs := getEnvInt("AUTO_DISPATCH_DELAY_MS", 30000)

	if jwtSecret == "" {
		log.Fatal("[FATAL] JWT_SECRET env var is required")
	}

	// Infraestructura
	cppClient := bridge.NewCppClient(cppAddr)
	mqProducer := mq.NewProducer(rabbitURL)
	defer mqProducer.Close()
	routePublisher := mq.NewRoutePublisher(mqProducer)

	// Optional event-driven auto-dispatch worker
	var autoDispatcher *worker.AutoDispatcher
	if autoDispatchEnabled {
		ad, err := worker.StartAutoDispatcher(
			rabbitURL,
			cppClient,
			routePublisher,
			autoDispatchEmployeeID,
			time.Duration(autoDispatchDelayMs)*time.Millisecond,
		)
		if err != nil {
			log.Printf("[WARN] AutoDispatcher disabled: failed to start: %v", err)
		} else {
			autoDispatcher = ad
			defer autoDispatcher.Stop()
		}
	}

	// Keeps in-memory employee routes in sync with delivered.* events.
	deliverySyncWorker, err := worker.StartDeliverySyncWorker(rabbitURL, routePublisher, cppClient)
	if err != nil {
		log.Printf("[WARN] DeliverySync disabled: failed to start: %v", err)
	} else if deliverySyncWorker != nil {
		defer deliverySyncWorker.Stop()
	}

	// Métricas
	metrics.Init()

	// Eureka
	eureka := registry.NewEurekaRegistry(eurekaURL, port)
	eureka.Register()
	defer eureka.Deregister()

	// Router
	r := mux.NewRouter()

	// Middleware global
	r.Use(metrics.Middleware)
	r.Use(middleware.AuthMiddleware(jwtSecret))
	r.Use(middleware.RateLimitMiddleware(rateLimitRPS))

	// Handlers
	r.HandleFunc("/shipments", handlers.CreateShipment(cppClient, mqProducer)).Methods("POST")
	r.HandleFunc("/status/{id}", handlers.GetStatus(cppClient)).Methods("GET")
	r.HandleFunc("/dispatch", handlers.Dispatch(cppClient, routePublisher)).Methods("POST")
	r.HandleFunc("/predict", handlers.Predict(predictorURL, time.Duration(predictorTimeout)*time.Millisecond)).Methods("GET")
	r.HandleFunc("/ws/chat", handlers.WSChat())
	r.HandleFunc("/metrics", metrics.HTTPHandler())
	r.HandleFunc("/health", healthHandler(cppClient, mqProducer))

	srv := &http.Server{
		Addr:    ":" + port,
		Handler: r,
	}

	go func() {
		log.Printf("[INFO] Backer listening on :%s", port)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("[ERROR] Server failed: %v", err)
		}
	}()

	// Graceful shutdown
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit
	log.Println("[INFO] Shutting down...")

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	if err := srv.Shutdown(ctx); err != nil {
		log.Fatalf("[ERROR] Forced shutdown: %v", err)
	}
	log.Println("[INFO] Backer stopped")
}

func healthHandler(cpp bridge.Bridge, mqProd mq.Publisher) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		status := map[string]string{
			"cpp_bridge": cpp.HealthStatus(),
			"rabbitmq":   mqProd.HealthStatus(),
		}
		code := http.StatusOK
		for _, v := range status {
			if v != "ok" {
				code = http.StatusServiceUnavailable
				break
			}
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(code)
		json.NewEncoder(w).Encode(status)
	}
}

func getEnv(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

func getEnvInt(key string, fallback int) int {
	if v := os.Getenv(key); v != "" {
		if n, err := strconv.Atoi(v); err == nil {
			return n
		}
	}
	return fallback
}

func getEnvBool(key string, fallback bool) bool {
	v := os.Getenv(key)
	if v == "" {
		return fallback
	}
	b, err := strconv.ParseBool(v)
	if err != nil {
		return fallback
	}
	return b
}
