package tests

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"backer/internal/handlers"
	"backer/internal/middleware"

	"github.com/gorilla/mux"
	"github.com/gorilla/websocket"
)

// wsRouter creates a router with JWT auth + WS handler.
func wsRouter() *mux.Router {
	r := mux.NewRouter()
	r.Use(middleware.AuthMiddleware(testSecret))
	r.HandleFunc("/ws/chat", handlers.WSChat())
	return r
}

// wsURL converts an httptest server URL (http://...) to a WebSocket URL (ws://...).
func wsURL(server *httptest.Server, path string) string {
	return "ws" + strings.TrimPrefix(server.URL, "http") + path
}

// ---------------------------------------------------------------------------
// JWT missing → 401 on upgrade
// ---------------------------------------------------------------------------

func TestWS_MissingJWT_Returns401(t *testing.T) {
	srv := httptest.NewServer(wsRouter())
	defer srv.Close()

	// Attempt upgrade without a token
	_, resp, err := websocket.DefaultDialer.Dial(wsURL(srv, "/ws/chat"), nil)
	if err == nil {
		t.Fatal("expected error on missing JWT, got successful upgrade")
	}
	if resp != nil && resp.StatusCode != http.StatusUnauthorized {
		t.Errorf("expected 401, got %d", resp.StatusCode)
	}
}

// ---------------------------------------------------------------------------
// Valid token → successful upgrade
// ---------------------------------------------------------------------------

func TestWS_ValidToken_Upgrades(t *testing.T) {
	srv := httptest.NewServer(wsRouter())
	defer srv.Close()

	// Pass JWT as query param (browsers can't set WS headers)
	url := wsURL(srv, "/ws/chat") + "?token=" + validToken()
	conn, _, err := websocket.DefaultDialer.Dial(url, nil)
	if err != nil {
		t.Fatalf("upgrade should succeed with valid token: %v", err)
	}
	defer conn.Close()
}

// ---------------------------------------------------------------------------
// Broadcast reaches registered client
// ---------------------------------------------------------------------------

func TestWS_BroadcastReachesClient(t *testing.T) {
	srv := httptest.NewServer(wsRouter())
	defer srv.Close()

	url := wsURL(srv, "/ws/chat") + "?token=" + validToken()
	conn, _, err := websocket.DefaultDialer.Dial(url, nil)
	if err != nil {
		t.Fatalf("upgrade failed: %v", err)
	}
	defer conn.Close()

	// Give the hub a moment to register the client
	time.Sleep(50 * time.Millisecond)

	// Broadcast an alert
	handlers.BroadcastAlert([]byte(`{"alert":"shipment_created"}`))

	// Read the message within a timeout
	conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	_, msg, err := conn.ReadMessage()
	if err != nil {
		t.Fatalf("expected to receive broadcast, got error: %v", err)
	}
	if !strings.Contains(string(msg), "shipment_created") {
		t.Errorf("unexpected message: %s", msg)
	}
}

// ---------------------------------------------------------------------------
// Slow client (full channel) is dropped without blocking others
// ---------------------------------------------------------------------------

func TestWS_SlowClientDropped(t *testing.T) {
	srv := httptest.NewServer(wsRouter())
	defer srv.Close()

	url := wsURL(srv, "/ws/chat") + "?token=" + validToken()

	// Connect a "fast" client that actively reads messages
	fastConn, _, err := websocket.DefaultDialer.Dial(url, nil)
	if err != nil {
		t.Fatalf("fast client upgrade failed: %v", err)
	}
	defer fastConn.Close()

	// Connect a "slow" client that never reads messages
	slowConn, _, err := websocket.DefaultDialer.Dial(url, nil)
	if err != nil {
		t.Fatalf("slow client upgrade failed: %v", err)
	}
	defer slowConn.Close()

	// Give the hub a moment to register both clients
	time.Sleep(50 * time.Millisecond)

	// Fast client reads continuously in background so its send buffer never fills up.
	received := make(chan string, 1024)
	go func() {
		for {
			_, msg, err := fastConn.ReadMessage()
			if err != nil {
				close(received)
				return
			}
			received <- string(msg)
		}
	}()

	// Send messages in batches with pauses so the fast client's writePump
	// has time to drain its send channel via websocket syscalls.
	// The slow client never reads, so its buffer (256) fills and it gets dropped.
	// 30 batches × 10 msgs = 300 total, well over the 256 buffer.
	for batch := 0; batch < 30; batch++ {
		for i := 0; i < 10; i++ {
			handlers.BroadcastAlert([]byte(`{"seq":"flood"}`))
		}
		time.Sleep(10 * time.Millisecond)
	}

	// Give the hub time to finish processing
	time.Sleep(200 * time.Millisecond)

	// Send a final message — fast client should still receive it
	handlers.BroadcastAlert([]byte(`{"alert":"after_flood"}`))

	// Wait for the marker message with a timeout
	timeout := time.After(3 * time.Second)
	found := false
	for !found {
		select {
		case msg, ok := <-received:
			if !ok {
				t.Fatal("fast client connection closed unexpectedly")
			}
			if strings.Contains(msg, "after_flood") {
				found = true
			}
		case <-timeout:
			t.Fatal("fast client should still receive messages after slow client overflow")
		}
	}
}
