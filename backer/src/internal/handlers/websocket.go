package handlers

import (
	"log"
	"net/http"
	"sync"
	"time"

	"backer/internal/metrics"

	"github.com/gorilla/websocket"
)

const (
	// Time allowed to write a message to the client
	writeWait = 10 * time.Second

	// Time allowed to read the next pong message from the client
	pongWait = 60 * time.Second

	// Send pings to the client with this period (must be less than pongWait)
	pingPeriod = (pongWait * 9) / 10

	// Maximum message size allowed from the client
	maxMessageSize = 2048

	// Size of the per-client send channel buffer.
	// A full buffer means the client is too slow — it gets dropped immediately
	// so one slow connection never blocks broadcasts to all others.
	sendBufferSize = 256
)

// upgrader configures the WebSocket upgrade.
// CheckOrigin allows all origins — in production Traefik handles this.
var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

// client represents a single connected WebSocket client.
type client struct {
	hub  *Hub
	conn *websocket.Conn
	send chan []byte // buffered channel of outbound messages
}

// Hub maintains the set of active clients and broadcasts messages to them.
// All registry mutations go through a single goroutine to avoid lock contention.
type Hub struct {
	mu         sync.RWMutex
	clients    map[*client]bool
	broadcast  chan []byte  // inbound messages to broadcast to all clients
	register   chan *client // register requests from new clients
	unregister chan *client // unregister requests from disconnecting clients
}

// hub is the singleton Hub instance used by all WebSocket connections.
var hub = newHub()

// newHub creates and starts the Hub's run loop.
func newHub() *Hub {
	h := &Hub{
		clients:    make(map[*client]bool),
		broadcast:  make(chan []byte, 256),
		register:   make(chan *client),
		unregister: make(chan *client),
	}
	go h.run()
	return h
}

// run is the Hub's main loop. All client registration and broadcast operations
// go through this single goroutine to avoid concurrent map writes.
func (h *Hub) run() {
	for {
		select {
		case c := <-h.register:
			h.mu.Lock()
			h.clients[c] = true
			h.mu.Unlock()
			metrics.IncWebsocket()
			log.Printf("[INFO] WS Hub: client registered, total=%d", len(h.clients))

		case c := <-h.unregister:
			h.mu.Lock()
			if _, ok := h.clients[c]; ok {
				delete(h.clients, c)
				close(c.send)
				metrics.DecWebsocket()
			}
			h.mu.Unlock()
			log.Printf("[INFO] WS Hub: client unregistered, total=%d", len(h.clients))

		case msg := <-h.broadcast:
			h.mu.RLock()
			for c := range h.clients {
				select {
				case c.send <- msg:
				default:
					// Client send buffer is full — drop and unregister asynchronously
					// to avoid deadlock with h.mu.RLock held
					go func(c *client) {
						h.unregister <- c
					}(c)
				}
			}
			h.mu.RUnlock()
		}
	}
}

// BroadcastAlert sends a message to all connected WebSocket clients.
// Called from shipment handlers when a status change occurs.
// Sends into a buffered channel so the caller is never blocked by fan-out latency.
func BroadcastAlert(msg []byte) {
	hub.broadcast <- msg
}

// readPump pumps messages from the WebSocket connection to the Hub.
// Runs in a dedicated goroutine for each client.
func (c *client) readPump() {
	defer func() {
		c.hub.unregister <- c
		c.conn.Close()
	}()

	c.conn.SetReadLimit(maxMessageSize)
	c.conn.SetReadDeadline(time.Now().Add(pongWait))
	c.conn.SetPongHandler(func(string) error {
		// Reset the read deadline on every pong received
		c.conn.SetReadDeadline(time.Now().Add(pongWait))
		return nil
	})

	for {
		_, msg, err := c.conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("[WARN] WS: unexpected close: %v", err)
			}
			break
		}
		// Broadcast incoming client messages to all other connected clients
		c.hub.broadcast <- msg
	}
}

// writePump pumps messages from the Hub to the WebSocket connection.
// Runs in a dedicated goroutine for each client.
func (c *client) writePump() {
	ticker := time.NewTicker(pingPeriod)
	defer func() {
		ticker.Stop()
		c.conn.Close()
	}()

	for {
		select {
		case msg, ok := <-c.send:
			c.conn.SetWriteDeadline(time.Now().Add(writeWait))
			if !ok {
				// Hub closed the channel — send a close message
				c.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}
			if err := c.conn.WriteMessage(websocket.TextMessage, msg); err != nil {
				log.Printf("[WARN] WS: write error: %v", err)
				return
			}

		case <-ticker.C:
			// Send a ping to keep the connection alive
			c.conn.SetWriteDeadline(time.Now().Add(writeWait))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

// WSChat handles GET /ws/chat.
// Upgrades the HTTP connection to WebSocket after JWT validation (handled by middleware).
// Each connection gets two goroutines: readPump and writePump.
func WSChat() http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			log.Printf("[ERROR] WS: upgrade failed: %v", err)
			return
		}

		c := &client{
			hub:  hub,
			conn: conn,
			send: make(chan []byte, sendBufferSize),
		}

		hub.register <- c

		// Each connection runs two goroutines that clean up after themselves
		go c.writePump()
		go c.readPump()
	}
}
