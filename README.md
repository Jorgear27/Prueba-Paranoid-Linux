# Paranoid Linux

---

# Building the system

## 1. Prerequisites

### Operating system
- Linux (tested on Ubuntu)

### Core tools
- CMake >= 3.15
- GCC and G++
- Conan 2.x
- Python 3

### Aligned packages
The QA workflow installs these packages:
- doxygen
- gcovr
- lcov
- cppcheck
- graphviz
- clang-format
- valgrind
- bc
- cmake
- gcc
- g++
- libmicrohttpd-dev
- python3
- postgresql
- postgresql-client

## 2. Services required

### PostgreSQL
The server uses PostgreSQL and expects a database and user prepared as below.

### MongoDB
The routing result persistence layer uses MongoDB.

If MongoDB is not reachable, the server still runs, but:
- saving results is skipped
- querying stored results returns empty

## 3. Start MongoDB with Docker (recommended)

Run MongoDB without installing it system-wide:

```bash
sudo docker run -d --name paranoid-mongo -p 27017:27017 mongo:7
```

Check container status:

```bash
sudo docker ps --filter name=paranoid-mongo
```

If needed, stop and remove it:

```bash
sudo docker stop paranoid-mongo
sudo docker rm paranoid-mongo
```

Mongo URI used by default:

```text
mongodb://localhost:27017
```

Override if needed:

```bash
export MONGO_URI="mongodb://localhost:27017"
```

## 4. Initialize PostgreSQL

Start PostgreSQL and prepare database and user.

From repository root:

```bash
sudo service postgresql start
sudo -u postgres psql -tc "SELECT 1 FROM pg_database WHERE datname='paranoid_db'" | grep -q 1 || sudo -u postgres psql -c "CREATE DATABASE paranoid_db;"
sudo -u postgres psql -tc "SELECT 1 FROM pg_roles WHERE rolname='server'" | grep -q 1 || sudo -u postgres psql -c "CREATE USER server WITH PASSWORD 'server123';"
sudo -u postgres psql -c "GRANT ALL PRIVILEGES ON DATABASE paranoid_db TO server;"
sudo -u postgres psql -d paranoid_db -f server/init/init_postgres.sql
```

## 5. Conan setup
The use of a python environment is recommended
```bash
python3 -m venv venv
source venv/bin/activate
pip install conan
`

```bash
conan --version
conan profile detect --force
conan remote update conancenter --url="https://center2.conan.io"
```

Install dependencies and generate CMake toolchain files:

```bash
conan install . -of build/Debug -s build_type=Debug --build=missing
```

## 6. Configure and build

From repository root:

```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=./build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DRUN_COVERAGE=TRUE
make -j$(nproc)
```

## 7. Run the server

From repository root after build:

```bash
./build/server/server
```

## 8. Start the Backer stack with Docker

The Backer services live in [backer/init/docker-compose.yml](backer/init/docker-compose.yml). This stack expects the native C++ server and PostgreSQL to already be running on the host.

From the `backer/init` directory, export the JWT secret and start the stack:

```bash
export JWT_SECRET="secret"
export AUTO_DISPATCH_ENABLED="true"
export AUTO_DISPATCH_EMPLOYEE_ID="E001"
export AUTO_DISPATCH_DELAY_MS="30000"
docker compose up --build
```

Notes:

- `AUTO_DISPATCH_ENABLED=true` enables event-driven dispatch from RabbitMQ shipment events.
- `AUTO_DISPATCH_DELAY_MS=30000` keeps a 30s cancellation window before auto-dispatch.
- `AUTO_DISPATCH_EMPLOYEE_ID` must match the courier simulator employee id.

If Docker access is restricted on your machine, either run the command with `sudo` or add your user to the `docker` group and log out/in again.

If you use `sudo`, pass the secret through explicitly:

```bash
sudo env JWT_SECRET="secret" AUTO_DISPATCH_ENABLED="true" AUTO_DISPATCH_EMPLOYEE_ID="E001" AUTO_DISPATCH_DELAY_MS="30000" docker compose up --build
```
---

# Testing Functionalities
## 1. Clients Simulation

To connect many hubs/warehouses automatically and exercise core flows use:

```bash
python3 scripts/LiveClients_GraphCreation_TCP-HTTP.py
```

Default scenario file:

- scripts/scenarios/multi_clients_simulation.json


By default this scenario reads:

- scripts/maps/graph_routing_map_7w_7h.json


Avoid http cheks if not needed:
```bash
python3 scripts/LiveClients_GraphCreation_TCP-HTTP.py --no-http-checks
```

## 2. Check Go functionalities

- **Health check:**
``` bash
curl http://localhost/health
```

- **First create a JWT token (required for all protected endpoints)**
``` bash
export JWT_SECRET="secret"

export TOKEN=$(python3 - <<'PY'
import base64, json, hmac, hashlib, time, os

secret = os.environ["JWT_SECRET"].encode()

def b64url(x):
    return base64.urlsafe_b64encode(x).rstrip(b'=').decode()

header = {"alg":"HS256","typ":"JWT"}
payload = {
    "uid":"demo-user",
    "role":"citizen",
    "exp": int(time.time()) + 3600
}

h = b64url(json.dumps(header,separators=(',',':')).encode())
p = b64url(json.dumps(payload,separators=(',',':')).encode())
s = b64url(hmac.new(secret, f"{h}.{p}".encode(), hashlib.sha256).digest())
print(f"{h}.{p}.{s}")
PY
)

echo "$TOKEN"
```

- **POST /shipments (auto-dispatch flow)**

Purpose: create a shipment order in the C++ core and publish a shipment event to RabbitMQ.
Auth: required.

``` bash
SHIPMENT_ID=$(curl -s -X POST http://localhost/shipments \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "origin_id":"H001",
    "destination_id":"W004",
    "items":[{"item_type":1,"quantity":10},{"item_type":2,"quantity":3}]
  }' | python3 -c 'import sys, json; print(json.load(sys.stdin)["shipment_id"])')

echo "$SHIPMENT_ID"
```

With auto-dispatch enabled, Backer will dispatch this shipment automatically after `AUTO_DISPATCH_DELAY_MS`
and publish route stops to the courier topic.

Backer now keeps a per-employee in-memory route queue: each dispatched shipment appends
its stops to the employee route (instead of replacing previous stops) and publishes
the full updated route to `routes/{employee_id}`.

Backer also consumes `delivered/{employee_id}` events and removes completed stops from
that employee route queue, so pending routes do not grow indefinitely.

The C++ core still returns shipment stops from its normal warehouse selection flow; Backer is responsible for accumulating those stops in the employee route queue before publishing to `routes/{employee_id}`.

- **GET /status/{id}**

Purpose: query shipment status from C++ core.
Auth: required.

``` bash
curl -i "http://localhost/status/$SHIPMENT_ID" \
  -H "Authorization: Bearer $TOKEN"
```

Optional: poll status until it transitions from pending to dispatched/delivered.

```bash
watch -n 2 "curl -s http://localhost/status/$SHIPMENT_ID -H 'Authorization: Bearer $TOKEN'"
```

- **POST /dispatch (optional manual override)**

Purpose:
- Delivered: mark shipment dispatched and publish route to courier topic routes/{employee_id}
- Canceled: cancel shipment
Auth: required.

You only need this when:

- Auto-dispatch is disabled (`AUTO_DISPATCH_ENABLED=false`), or
- You want to force a manual dispatch/cancel action.

Delivered:
``` bash
curl -i -X POST http://localhost/dispatch \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "shipment_id":"'$SHIPMENT_ID'",
    "status":"Delivered",
    "employee_id":"E001",
    "stops":["W003","W007","H001"]
  }'
```

Canceled (in the 30s window):
``` bash
curl -i -X POST http://localhost/dispatch \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "shipment_id":"'$SHIPMENT_ID'",
    "status":"Canceled"
  }'
```

- **GET /predict**

Purpose: ask ML predictor for ETA/cost/box size; if predictor fails, returns fallback estimate.
Auth: required.

``` bash
curl -i "http://localhost/predict?origin_id=H001&destination_id=W001&weight_kg=10&total_qty=25" \
  -H "Authorization: Bearer $TOKEN"
```

- **GET /metrics**

Purpose: Prometheus metrics endpoint.
Auth: not required.

``` bash
curl -s http://localhost/metrics | head -n 40
```


## 3. Run Courier simulator

### Qt desktop simulator

The simulator is useful to visualize route reception, GPS tracking, SOS, and delivery events.

```bash
sudo apt-get install -y qtbase5-dev libmosquitto-dev pkg-config mosquitto-clients
cd courier/sim
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DEMPLOYEE_ID='"E001"' \
  -DMQTT_BROKER_ADDR='"127.0.0.1"' \
  -DMQTT_BROKER_PORT=1883
cmake --build build -j
COURIER_EMPLOYEE_ID=E001 COURIER_BROKER_ADDR=127.0.0.1 COURIER_BROKER_PORT=1883 ./build/courier_sim
```

### Courier functionality checks

In separate terminals, subscribe to the topics the courier uses:

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t routes/E001 -v
mosquitto_sub -h 127.0.0.1 -p 1883 -t tracking/E001 -v
mosquitto_sub -h 127.0.0.1 -p 1883 -t alerts/sos/E001 -v
mosquitto_sub -h 127.0.0.1 -p 1883 -t delivered/E001 -v
```

Or we can visualize it on RabbitMQ:

Backer publishes to amq.topic with AMQP key like routes.E001.
MQTT plugin maps dots to slashes. Courier receives MQTT topic routes/E001.

- Create queue debug.routes
Add binding:
- From exchange amq.topic
- Routing keys:
  - routes.#
  - tracking.#
  - alerts.sos.#
  - delivered.#

Expected behavior:

- The UI shows the current stop and the next destination.
- The route can contain multiple warehouse stops as shipments are appended to the employee route queue.
- Tracking is published every 5 seconds.
- SOS publishes a JSON alert to `alerts/sos/{employee_id}`.
- Delivered publishes `{"stop":"<name>","status":"done"}` to `delivered/{employee_id}`.

## 4. Graphical User Interfaces (Endpoints)

- **[RabbitMQ](http://localhost:15672)**

RabbitMQ Management inspects and tests messaging flows

> Exchange: entry point where publishers send messages
> Queue: storage for messages
> Binding: rule connecting exchange -> queue
> Routing key: label used (especially in topic exchange)

- Here yo can create debug queues:
debug.shipments
debug.routes

- Bind queues:
debug.shipments bound to shipments exchange
debug.routes bound to amq.topic with key like routes.E001

We can also publish methods directly from RabbitMQ


- **[Prometheus](http://localhost:9091)**

Prometheus runs queries in order to visualize metrics

Run `backer_requests_total` query

- **[Traefik](http://localhost:80)**

Requests hit Traefik first, then it forwards to Backer service.
Can add load balancing, middleware, circuit breaker, TLS, routing rules.


- **[Eureka](http://localhost:8761)**

Service discovery registry. Services register themselves (Backer registers as `BACKER`)
Other services can discover healthy instances dynamically

It becomes useful when we have many services/instances that change addresses frequently.

---

# Run tests

## 1. Server and Clients test
This tests all core functionalites of the server (c++) and the clients (c), including graph routing algorithms.

From repository root:

```bash
ctest --test-dir build --output-on-failure
```

## 2. Full graph routing simulation test

This scenario validates end-to-end behavior:

- client authentication
- inventory sync from warehouses
- order creation from a hub
- waiting through the 30s cancellation window
- warehouse dispatch and hub delivery update
- warehouse restocks
- HTTP graph algorithm checks before and after restocks

Run:

```bash
python3 scripts/LiveClients_LifeCycle_GraphRefresh_TCP-HTTP.py
```

Default scenario file:

- scripts/scenarios/order_lifecycle_simulation.json

Useful options:

```bash
python3 scripts/LiveClients_LifeCycle_GraphRefresh_TCP-HTTP.py --scenario scripts/scenarios/order_lifecycle_simulation.json
```

```bash
python3 scripts/LiveClients_LifeCycle_GraphRefresh_TCP-HTTP.py --no-http-checks
```

```bash
python3 scripts/LiveClients_LifeCycle_GraphRefresh_TCP-HTTP.py --no-disconnect
```

## 3. Run Backer Go tests

If you have Go 1.26.x or newer installed locally:

```bash
cd backer/src
go test ./tests/... -v
go test ./... -v
```

If your local Go version is older, use Docker instead:

```bash
cd backer/src
docker run --rm -v "$PWD":/app -w /app golang:1.26-alpine sh -lc "go mod download && go test ./tests/... -v
```

## 4. Run Courier tests and simulator (Unity)

The Unity test framework is vendored as a submodule under [courier/src/unity](courier/src/unity).

If this is a fresh clone, initialize the submodule first:

```bash
git submodule update --init --recursive
```

Run the host test binary:

```bash
cd courier/src
gcc -I. -Iunity/src test_mqtt_handler.c courier_state.c unity/src/unity.c -lpthread -o test_mqtt_handler
./test_mqtt_handler
```

# Visual utilites

## 1. Visual Graph Views (Topology + Algorithms)

You can generate visual files from the fixture map to inspect:

- Full topology (warehouses + hubs + directed connections)
- Bellman-Ford shortest-path tree over markets
- Ford-Fulkerson max-flow overlay over fulfillment centers
- Kaufmann-Malgrange Hamiltonian circuit overlay over fulfillment centers

### Generate views

```bash
chmod +x scripts/Maps_Visualization.sh
./scripts/Maps_Visualization.sh
```

By default this reads:

- scripts/fixtures/graph_routing_map_5w_4h.json

And writes to:

- scripts/output/graphs/

Generated DOT files:

- topology.dot
- markets_shortest_paths.dot
- fulfillment_max_flow.dot
- fulfillment_hamiltonian.dot

If Graphviz is installed (`dot` command), PNG and SVG are generated automatically.

### Useful options

```bash
MARKET_SOURCE=H001 FC_SOURCE=W001 FC_SINK=W005 ./scripts/Maps_Visualization.sh
```

```bash
OUT_DIR=./tmp/graph-views ./scripts/Maps_Visualization.sh
```

```bash
FIXTURE_PATH=./scripts/fixtures/graph_routing_map_5w_4h.json ./scripts/Maps_Visualization.sh
```

Install Graphviz if needed:

```bash
sudo apt-get install -y graphviz
```
