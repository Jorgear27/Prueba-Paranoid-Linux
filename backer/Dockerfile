# ---- Stage 1: Build ----
    FROM golang:1.26-alpine AS builder

    RUN apk add --no-cache git
    
    WORKDIR /build
    
    # Cache dependencies first
    COPY src/go.mod src/go.sum ./
    RUN go mod download
    
    # Copy source and build a static, stripped binary
    COPY src/ ./
    RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" -o /backer ./cmd/
    
    # ---- Stage 2: Runtime ----
    FROM alpine:3.19
    
    RUN apk add --no-cache ca-certificates \
        && adduser -D -h /home/backer backer
    
    COPY --from=builder /backer /usr/local/bin/backer
    
    USER backer
    
    EXPOSE 9090
    
    HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
        CMD wget -qO /dev/null http://localhost:9090/metrics || exit 1
    
    ENTRYPOINT ["backer"]