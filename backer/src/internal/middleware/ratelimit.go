package middleware

import (
	"net/http"
	"strings"
	"sync"
	"time"

	"golang.org/x/time/rate"
)

// entry holds the limiter for a single IP and the last time it was seen.
// Used by the cleanup goroutine to evict stale entries.
type entry struct {
	limiter  *rate.Limiter
	lastSeen time.Time
}

// ipLimiter manages a per-IP token-bucket map with background cleanup.
type ipLimiter struct {
	mu      sync.Mutex
	entries map[string]*entry
	rps     int
}

// newIPLimiter creates the map and starts the background pruning goroutine.
// Entries not seen for more than 5 minutes are removed to prevent unbounded growth.
func newIPLimiter(rps int) *ipLimiter {
	il := &ipLimiter{
		entries: make(map[string]*entry),
		rps:     rps,
	}
	go il.cleanup()
	return il
}

// get returns the rate limiter for the given IP, creating one if it does not exist.
func (il *ipLimiter) get(ip string) *rate.Limiter {
	il.mu.Lock()
	defer il.mu.Unlock()

	e, ok := il.entries[ip]
	if !ok {
		e = &entry{
			limiter: rate.NewLimiter(rate.Limit(il.rps), il.rps),
		}
		il.entries[ip] = e
	}
	e.lastSeen = time.Now()
	return e.limiter
}

// cleanup removes entries that have not been seen in the last 5 minutes.
// Runs in a background goroutine for the lifetime of the server.
func (il *ipLimiter) cleanup() {
	ticker := time.NewTicker(time.Minute)
	defer ticker.Stop()
	for range ticker.C {
		il.mu.Lock()
		for ip, e := range il.entries {
			if time.Since(e.lastSeen) > 5*time.Minute {
				delete(il.entries, ip)
			}
		}
		il.mu.Unlock()
	}
}

// RateLimitMiddleware returns a middleware that limits requests per IP.
// The rate is configurable via the rps parameter (set from RATE_LIMIT_RPS env var).
// Reads X-Forwarded-For set by Traefik so the bucket is per real IP, not per proxy.
func RateLimitMiddleware(rps int) func(http.Handler) http.Handler {
	limiter := newIPLimiter(rps)

	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			ip := realIP(r)

			if !limiter.get(ip).Allow() {
				http.Error(w, `{"error":"rate limit exceeded"}`, http.StatusTooManyRequests)
				return
			}

			next.ServeHTTP(w, r)
		})
	}
}

// realIP extracts the real client IP from X-Forwarded-For (set by Traefik)
// or falls back to RemoteAddr if the header is absent.
func realIP(r *http.Request) string {
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		// X-Forwarded-For can be a comma-separated list; the first entry is the real client
		parts := strings.Split(xff, ",")
		return strings.TrimSpace(parts[0])
	}
	// RemoteAddr is in the form "ip:port" — strip the port
	ip := r.RemoteAddr
	if idx := strings.LastIndex(ip, ":"); idx != -1 {
		ip = ip[:idx]
	}
	return ip
}
