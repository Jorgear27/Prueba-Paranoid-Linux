# 1) Load any map (5W/4H) in this case
curl -s -X POST http://127.0.0.1:8081/map \
  -H "Content-Type: application/json" \
  --data-binary @scripts/maps/graph_routing_map_5w_4h.json \
  | python3 -m json.tool

# 2) Bellman-Ford from H001
curl -s -X POST http://127.0.0.1:8081/markets-path \
  -H "Content-Type: application/json" \
  -d '{"source_id":"H001"}' \
  | python3 -m json.tool

# 3) Ford-Fulkerson from W001 to W005
curl -s -X POST http://127.0.0.1:8081/fulfillment-flow \
  -H "Content-Type: application/json" \
  -d '{"source_id":"W001","sink_id":"W005"}' \
  | python3 -m json.tool

# 4) Kaufmann-Malgrange (Hamiltonian circuit check)
curl -s -X POST http://127.0.0.1:8081/fulfillment-circuit \
  -H "Content-Type: application/json" \
  -d '{}' \
  | python3 -m json.tool

# 5) Query specific algorithm results (bellman-ford)
curl -s "http://127.0.0.1:8081/results/?algorithm=bellman_ford&limit=20" | python3 -m json.tool

# 6) Query maps
curl -s "http://127.0.0.1:8081/results/?algorithm=map&limit=10" | python3 -m json.tool
