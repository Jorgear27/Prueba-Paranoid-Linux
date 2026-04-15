# Paranoid Linux

Setup and local development guide for the Paranoid Linux project.

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

## 4. Start the Backer stack with Docker

The Backer services live in [backer/init/docker-compose.yml](backer/init/docker-compose.yml). This stack expects the native C++ server and PostgreSQL to already be running on the host.

From the `backer/init` directory, export the JWT secret and start the stack:

```bash
export JWT_SECRET="secret"
docker compose up --build
```

If Docker access is restricted on your machine, either run the command with `sudo` or add your user to the `docker` group and log out/in again.

If you use `sudo`, pass the secret through explicitly:

```bash
sudo env JWT_SECRET="secret" docker compose up --build
```

## 5. Initialize PostgreSQL

Start PostgreSQL and prepare database and user.

From repository root:

```bash
sudo service postgresql start
sudo -u postgres psql -tc "SELECT 1 FROM pg_database WHERE datname='paranoid_db'" | grep -q 1 || sudo -u postgres psql -c "CREATE DATABASE paranoid_db;"
sudo -u postgres psql -tc "SELECT 1 FROM pg_roles WHERE rolname='server'" | grep -q 1 || sudo -u postgres psql -c "CREATE USER server WITH PASSWORD 'server123';"
sudo -u postgres psql -c "GRANT ALL PRIVILEGES ON DATABASE paranoid_db TO server;"
sudo -u postgres psql -d paranoid_db -f server/init/init_postgres.sql
```

## 6. Conan setup
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

## 7. Configure and build

From repository root:

```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=./build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DRUN_COVERAGE=TRUE
make -j$(nproc)
```

## 8. Run binaries

From repository root after build:

```bash
./build/server/server
./build/hub/hub
./build/warehouse/warehouse
```

## 9. Run tests

From repository root:

```bash
ctest --test-dir build --output-on-failure
```

## 10. Troubleshooting

### Error: Package mongo-cxx-driver not resolved
- Ensure conancenter points to center2:
  - conan remote update conancenter --url="https://center2.conan.io"

### Error: conan_toolchain.cmake not found
- Run Conan install first:
  - conan install . -of build/Debug -s build_type=Debug --build=missing

### Error: could not connect to MongoDB at localhost:27017
- Start MongoDB container:
  - sudo docker run -d --name paranoid-mongo -p 27017:27017 mongo:7

## 11. Visual Graph Views (Topology + Algorithms)

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

## 12. Clients Simulation

To connect many hubs/warehouses automatically and exercise core TCP flows
(authentication, inventory updates, restock, order request, order status), use:

```bash
python3 scripts/LiveClients_GraphCreation_TCP-HTTP.py
```

Default scenario file:

- scripts/scenarios/multi_clients_simulation.json


By default this scenario reads:

- scripts/maps/graph_routing_map_7w_7h.json


Useful options:

```bash
python3 scripts/LiveClients_GraphCreation_TCP-HTTP.py --scenario scripts/scenarios/multi_clients_simulation.json
```

```bash
python3 scripts/LiveClients_GraphCreation_TCP-HTTP.py --host 127.0.0.1 --port 8080 --no-http-checks
```

```bash
python3 scripts/LiveClients_GraphCreation_TCP-HTTP.py --no-core-flows --no-disconnect
```

Notes:

- Start `./build/server/server` first.
- Keep MongoDB/PostgreSQL running if you want persistent graph/results behavior.
- The simulator opens one TCP connection per node in the map, prints server responses, and then closes cleanly.


## 13. Full order lifecycle simulation (30s window + restock + graph checks)

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

## 14. Run Backer Go tests

The Backer tests live in [backer/src/tests](backer/src/tests).

If you have Go 1.26.x or newer installed locally:

```bash
cd backer/src
go test ./tests/... -v
go test ./... -v
```

If your local Go version is older, use Docker instead:

```bash
cd backer/src
docker run --rm -v "$PWD":/app -w /app golang:1.26-alpine sh -lc "go mod download && go test ./tests/... -v"
```

Useful checks for Backer functionality:

- `POST /shipments` should return `shipment_id`, `status`, and the courier `stops` list.
- `POST /dispatch` with `status: "Delivered"` should require `employee_id` and `stops`.
- `GET /health` should report `cpp_bridge` and `rabbitmq` as `ok`.

## 15. Run Courier tests and simulator

The Courier firmware lives in [courier](courier). It has two test paths:

### Host unit tests

These validate the shared courier state and payload builders on your machine.
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

Publish a route to see the UI update:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t routes/E001 -m '["Mercado Sur","Mercado Norte"]'
```

Expected behavior:

- The UI shows the current stop and the next destination.
- Tracking is published every 5 seconds.
- SOS publishes a JSON alert to `alerts/sos/{employee_id}`.
- Delivered publishes `{"stop":"<name>","status":"done"}` to `delivered/{employee_id}`.
