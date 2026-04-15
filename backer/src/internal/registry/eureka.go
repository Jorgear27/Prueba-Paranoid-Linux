package registry

import (
	"bytes"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"time"
)

// eurekaRegistrationBody is the payload sent to Eureka on registration.
// Follows the Eureka REST API schema.
type eurekaRegistrationBody struct {
	Instance eurekaInstance `json:"instance"`
}

type eurekaInstance struct {
	InstanceID string     `json:"instanceId"`
	App        string     `json:"app"`
	HostName   string     `json:"hostName"`
	IPAddr     string     `json:"ipAddr"`
	Port       eurekaPort `json:"port"`
	Status     string     `json:"status"`
	HealthURL  string     `json:"healthCheckUrl"`
}

type eurekaPort struct {
	Port    int  `json:"$"`
	Enabled bool `json:"@enabled"`
}

// EurekaRegistry handles registration, heartbeats and deregistration
// with the Eureka service discovery server.
type EurekaRegistry struct {
	url        string
	port       string
	instanceID string
	appName    string
}

// NewEurekaRegistry creates a registry client for the Backer service.
func NewEurekaRegistry(url, port string) *EurekaRegistry {
	return &EurekaRegistry{
		url:        url,
		port:       port,
		instanceID: fmt.Sprintf("backer:%s", port),
		appName:    "BACKER",
	}
}

// Register sends the initial registration request to Eureka and starts
// the heartbeat goroutine. Registration failure is non-fatal — the server
// continues operating without service discovery, following the same defensive
// approach used in TP1/TP2 (MongoDB failure does not prevent the C++ server from starting).
func (e *EurekaRegistry) Register() {
	portInt := 9090
	fmt.Sscanf(e.port, "%d", &portInt)

	body := eurekaRegistrationBody{
		Instance: eurekaInstance{
			InstanceID: e.instanceID,
			App:        e.appName,
			HostName:   "backer",
			IPAddr:     "backer",
			Port:       eurekaPort{Port: portInt, Enabled: true},
			Status:     "UP",
			HealthURL:  fmt.Sprintf("http://backer:%s/health", e.port),
		},
	}

	data, err := json.Marshal(body)
	if err != nil {
		log.Printf("[WARN] Eureka: failed to marshal registration body: %v", err)
		return
	}

	url := fmt.Sprintf("%s/apps/%s", e.url, e.appName)
	resp, err := http.Post(url, "application/json", bytes.NewReader(data))
	if err != nil {
		log.Printf("[WARN] Eureka: registration failed (server may not be running): %v", err)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusNoContent {
		log.Printf("[WARN] Eureka: unexpected registration response: %d", resp.StatusCode)
		return
	}

	log.Printf("[INFO] Eureka: registered as %s", e.instanceID)

	// Start heartbeat goroutine — sends PUT every 30s to keep registration alive
	go e.heartbeat()
}

// heartbeat sends a PUT request to Eureka every 30 seconds to renew the lease.
// If the heartbeat fails, it logs a warning but does not crash the server.
func (e *EurekaRegistry) heartbeat() {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for range ticker.C {
		url := fmt.Sprintf("%s/apps/%s/%s", e.url, e.appName, e.instanceID)
		req, err := http.NewRequest(http.MethodPut, url, nil)
		if err != nil {
			log.Printf("[WARN] Eureka: heartbeat request error: %v", err)
			continue
		}

		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			log.Printf("[WARN] Eureka: heartbeat failed: %v", err)
			continue
		}
		resp.Body.Close()
		log.Printf("[INFO] Eureka: heartbeat sent, status=%d", resp.StatusCode)
	}
}

// Deregister sends a DELETE request to Eureka on graceful shutdown.
// Called via defer in main.go when the server receives SIGINT or SIGTERM.
func (e *EurekaRegistry) Deregister() {
	url := fmt.Sprintf("%s/apps/%s/%s", e.url, e.appName, e.instanceID)
	req, err := http.NewRequest(http.MethodDelete, url, nil)
	if err != nil {
		log.Printf("[WARN] Eureka: deregister request error: %v", err)
		return
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		log.Printf("[WARN] Eureka: deregister failed: %v", err)
		return
	}
	defer resp.Body.Close()
	log.Printf("[INFO] Eureka: deregistered %s", e.instanceID)
}
