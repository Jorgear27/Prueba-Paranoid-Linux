package bridge

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"sync"
	"time"

	"backer/internal/metrics"
)

// Bridge is the interface used by handlers to communicate with the C++ core.
// CppClient is the production implementation; tests can provide fakes.
type Bridge interface {
	SendRequest(ctx context.Context, payload any) (map[string]any, error)
	HealthStatus() string
}

// Tipos de request que el C++ RequestRouter reconoce
type OrderRequest struct {
	Type    string      `json:"type"`
	HubID   string      `json:"hub_id"`
	OrderID string      `json:"order_id"`
	Items   []OrderItem `json:"items_needed"`
}

type OrderItem struct {
	ItemType int `json:"item_type"`
	Quantity int `json:"quantity"`
}

type StatusQuery struct {
	Type      string `json:"type"`
	HubID     string `json:"hub_id"`
	OrderID   string `json:"order_id"`
	Timestamp string `json:"timestamp"`
}

type DispatchMsg struct {
	Type         string      `json:"type"`
	OrderID      string      `json:"order_id"`
	Status       string      `json:"status"`
	ItemsShipped []OrderItem `json:"items_shipped"`
}

type CancelMsg struct {
	Type    string `json:"type"`
	OrderID string `json:"order_id"`
}

type CppClient struct {
	addr string
	conn net.Conn
	mu   sync.Mutex
}

func NewCppClient(addr string) *CppClient {
	c := &CppClient{addr: addr}
	// Intento de conexión inicial, no fatal si falla
	c.connect()
	return c
}

func (c *CppClient) connect() error {
	conn, err := net.DialTimeout("tcp", c.addr, 5*time.Second)
	if err != nil {
		log.Printf("[WARN] C++ bridge: could not connect to %s: %v", c.addr, err)
		return err
	}
	c.conn = conn
	log.Printf("[INFO] C++ bridge: connected to %s", c.addr)
	return nil
}

func (c *CppClient) reconnectWithBackoff(ctx context.Context) error {
	delay := time.Second
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}
		if err := c.connect(); err == nil {
			return nil
		}
		log.Printf("[WARN] C++ bridge: retrying in %s", delay)
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(delay):
		}
		delay *= 2
		if delay > 30*time.Second {
			delay = 30 * time.Second
		}
	}
}

// SendRequest serializa el struct, lo manda al C++ core y devuelve la respuesta JSON
func (c *CppClient) SendRequest(ctx context.Context, payload any) (map[string]any, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	data, err := json.Marshal(payload)
	if err != nil {
		metrics.IncCppBridge("error")
		return nil, fmt.Errorf("marshal error: %w", err)
	}
	data = append(data, '\n')

	// Si no hay conexión, intentar reconectar
	if c.conn == nil {
		if err := c.reconnectWithBackoff(ctx); err != nil {
			metrics.IncCppBridge("error")
			return nil, fmt.Errorf("cpp core unreachable: %w", err)
		}
	}

	// Escribir request
	if _, err := c.conn.Write(data); err != nil {
		c.conn = nil
		metrics.IncCppBridge("error")
		return nil, fmt.Errorf("cpp core unreachable: %w", err)
	}

	// Leer respuesta
	resp, err := c.readResponse()
	if err != nil {
		c.conn = nil
		metrics.IncCppBridge("error")
		return nil, fmt.Errorf("cpp core unreachable: %w", err)
	}

	metrics.IncCppBridge("ok")
	return resp, nil
}

func (c *CppClient) readResponse() (map[string]any, error) {
	buf := make([]byte, 0, 1024)
	tmp := make([]byte, 256)

	for {
		n, err := c.conn.Read(tmp)
		if err != nil {
			return nil, err
		}
		buf = append(buf, tmp[:n]...)
		if isCompleteJSON(buf) {
			break
		}
	}

	var result map[string]any
	if err := json.Unmarshal(buf, &result); err != nil {
		return nil, fmt.Errorf("invalid JSON from core: %w", err)
	}
	return result, nil
}

// isCompleteJSON detecta un objeto JSON completo contando llaves balanceadas
func isCompleteJSON(data []byte) bool {
	depth := 0
	inStr := false
	escaped := false

	for _, b := range data {
		if escaped {
			escaped = false
			continue
		}
		if b == '\\' && inStr {
			escaped = true
			continue
		}
		if b == '"' {
			inStr = !inStr
			continue
		}
		if inStr {
			continue
		}
		if b == '{' {
			depth++
		} else if b == '}' {
			depth--
			if depth == 0 {
				return true
			}
		}
	}
	return false
}

func (c *CppClient) HealthStatus() string {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.conn == nil {
		return "unreachable"
	}
	return "ok"
}
