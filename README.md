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

## 7. Run binaries

From repository root after build:

```bash
./build/server/server
./build/hub/hub
./build/warehouse/warehouse
```

## 8. Run tests

From repository root:

```bash
ctest --test-dir build --output-on-failure
```

## 9. Troubleshooting

### Error: Package mongo-cxx-driver not resolved
- Ensure conancenter points to center2:
  - conan remote update conancenter --url="https://center2.conan.io"

### Error: conan_toolchain.cmake not found
- Run Conan install first:
  - conan install . -of build/Debug -s build_type=Debug --build=missing

### Error: could not connect to MongoDB at localhost:27017
- Start MongoDB container:
  - sudo docker run -d --name paranoid-mongo -p 27017:27017 mongo:7

## 10. Visual Graph Views (Topology + Algorithms)

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

## 11. Clients Simulation

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


## 12. Full order lifecycle simulation (30s window + restock + graph checks)

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
