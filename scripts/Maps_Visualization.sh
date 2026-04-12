#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURE_PATH="${FIXTURE_PATH:-$ROOT_DIR/scripts/maps/graph_routing_map_8w_8h.json}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/scripts/graphs}"

MARKET_SOURCE="${MARKET_SOURCE:-H001}"
FC_SOURCE="${FC_SOURCE:-W001}"
FC_SINK="${FC_SINK:-W008}"

mkdir -p "$OUT_DIR"

if ! command -v python3 >/dev/null 2>&1; then
    echo "[ERROR] python3 is required." >&2
    exit 1
fi

if [[ ! -f "$FIXTURE_PATH" ]]; then
    echo "[ERROR] Fixture file not found: $FIXTURE_PATH" >&2
    exit 1
fi

echo "[INFO] Rendering graph views from: $FIXTURE_PATH"
echo "[INFO] Output directory: $OUT_DIR"

python3 - "$FIXTURE_PATH" "$OUT_DIR" "$MARKET_SOURCE" "$FC_SOURCE" "$FC_SINK" <<'PY'
import json
import math
import os
import sys
from collections import defaultdict, deque

fixture_path, out_dir, market_source, fc_source, fc_sink = sys.argv[1:6]

with open(fixture_path, "r", encoding="utf-8") as f:
    nodes = json.load(f)

node_by_id = {}
for n in nodes:
    node_by_id[n["node_id"]] = n

def node_kind(node_type):
    return "Market" if node_type == "market" else "FulfillmentCenter"

type_mod = {
    "rail": 0.7,
    "waterway": 0.9,
    "road": 1.0,
    "tunnel": 1.1,
    "drone": 1.2,
    "trail": 1.3,
    "bridge": 1.4,
    "manual": 1.6,
    "blocked": float("inf"),
}

cond_mod = {
    "reinforced": -0.3,
    "cleared": -0.2,
    "foggy": 0.1,
    "rain": 0.2,
    "infected_activity": 0.3,
}

def edge_cost(conn):
    base = float(conn.get("base_weight", 0.0))
    if base <= 0.0:
        return float("inf")
    t = conn.get("connection_type", "road")
    tm = type_mod.get(t, 1.0)
    if math.isinf(tm):
        return float("inf")
    cs = conn.get("connection_conditions", [])
    total = 1.0 + sum(cond_mod.get(c, 0.0) for c in cs)
    if total <= 0.0:
        return float("inf")
    return base * tm * total

def edge_cap(conn):
    base = float(conn.get("base_weight", 0.0))
    return int(base) if base > 0 else 0

def get_edges(kind):
    edges = []
    for n in nodes:
        if node_kind(n["node_type"]) != kind:
            continue
        u = n["node_id"]
        for c in n.get("connections", []):
            v = c.get("target_node_id")
            if v not in node_by_id:
                continue
            if node_kind(node_by_id[v]["node_type"]) != kind:
                continue
            edges.append((u, v, c))
    return edges

market_edges = get_edges("Market")
fc_edges = get_edges("FulfillmentCenter")

market_nodes = [n["node_id"] for n in nodes if node_kind(n["node_type"]) == "Market"]
fc_nodes = [n["node_id"] for n in nodes if node_kind(n["node_type"]) == "FulfillmentCenter"]

def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)

def q(s):
    return '"' + s.replace('"', '\\"') + '"'

def write_topology_dot():
    lines = []
    lines.append("digraph Topology {")
    # Top-to-bottom layout reduces horizontal sprawl on dense maps.
    lines.append("  rankdir=TB;")
    lines.append("  splines=true;")
    lines.append("  overlap=false;")
    lines.append("  nodesep=0.35;")
    lines.append("  ranksep=0.75;")
    lines.append("  labelloc=t;")
    lines.append("  label=\"Routing Topology (Fixture)\";")
    lines.append("  node [shape=ellipse, style=filled, fontname=Helvetica, fontsize=10];")
    lines.append("  edge [fontsize=9];")

    for n in nodes:
        nid = n["node_id"]
        t = n["node_type"]
        color = "#ffd166" if t == "market" else "#8ecae6"
        lines.append(f"  {q(nid)} [fillcolor=\"{color}\", label=\"{nid}\\n{t}\"];")

    for n in nodes:
        u = n["node_id"]
        for c in n.get("connections", []):
            v = c.get("target_node_id")
            if v not in node_by_id:
                continue
            bw = c.get("base_weight", 0)
            ct = c.get("connection_type", "road")
            cc = c.get("connection_conditions", [])
            cond_txt = "none" if not cc else ",".join(cc)
            lbl = f"w={bw}\\n{ct}\\n{cond_txt}"
            lines.append(f"  {q(u)} -> {q(v)} [label=\"{lbl}\", color=\"#555\"];")

    lines.append("}")
    write(os.path.join(out_dir, "topology.dot"), "\n".join(lines) + "\n")

def bellman_ford_tree(source):
    dist = {m: float("inf") for m in market_nodes}
    pred = {}
    if source not in dist:
        return dist, pred
    dist[source] = 0.0

    weighted = []
    for u, v, c in market_edges:
        w = edge_cost(c)
        if math.isinf(w):
            continue
        weighted.append((u, v, w))

    for _ in range(max(0, len(market_nodes) - 1)):
        changed = False
        for u, v, w in weighted:
            if math.isinf(dist[u]):
                continue
            cand = dist[u] + w
            if cand < dist[v]:
                dist[v] = cand
                pred[v] = u
                changed = True
        if not changed:
            break
    return dist, pred

def write_markets_shortest_paths_dot(source):
    dist, pred = bellman_ford_tree(source)
    tree_edges = {(u, v) for v, u in pred.items()}

    lines = []
    lines.append("digraph MarketShortestPaths {")
    lines.append("  rankdir=LR;")
    lines.append("  labelloc=t;")
    lines.append(f"  label=\"Bellman-Ford Tree from {source}\";")
    lines.append("  node [shape=ellipse, style=filled, fontname=Helvetica, fillcolor=\"#ffd166\"];")

    for m in market_nodes:
        d = dist.get(m, float("inf"))
        dtxt = "INF" if math.isinf(d) else f"{d:.2f}"
        border = "#d00000" if m == source else "#333333"
        pen = "2.4" if m == source else "1.0"
        lines.append(f"  {q(m)} [label=\"{m}\\nd={dtxt}\", color=\"{border}\", penwidth={pen}];")

    for u, v, c in market_edges:
        w = edge_cost(c)
        lbl = "blocked" if math.isinf(w) else f"{w:.2f}"
        if (u, v) in tree_edges:
            lines.append(f"  {q(u)} -> {q(v)} [label=\"{lbl}\", color=\"#2a9d8f\", penwidth=2.4];")
        else:
            lines.append(f"  {q(u)} -> {q(v)} [label=\"{lbl}\", color=\"#999\", style=dashed];")

    lines.append("}")
    write(os.path.join(out_dir, "markets_shortest_paths.dot"), "\n".join(lines) + "\n")

def max_flow(src, sink):
    residual = defaultdict(lambda: defaultdict(int))
    original = defaultdict(lambda: defaultdict(int))

    for u, v, c in fc_edges:
        cap = edge_cap(c)
        if cap <= 0:
            continue
        residual[u][v] += cap
        residual[v][u] += 0
        original[u][v] += cap

    def bfs(parent):
        parent.clear()
        seen = {src}
        qd = deque([src])
        while qd:
            u = qd.popleft()
            for v, cap in residual[u].items():
                if v not in seen and cap > 0:
                    parent[v] = u
                    if v == sink:
                        return True
                    seen.add(v)
                    qd.append(v)
        return False

    if src not in fc_nodes or sink not in fc_nodes:
        return 0, {}

    parent = {}
    value = 0
    while bfs(parent):
        b = 10**18
        v = sink
        while v != src:
            u = parent[v]
            b = min(b, residual[u][v])
            v = u
        v = sink
        while v != src:
            u = parent[v]
            residual[u][v] -= b
            residual[v][u] += b
            v = u
        value += b

    flow = {}
    for u, dst in original.items():
        for v, cap in dst.items():
            rem = residual[u][v]
            flow[(u, v)] = cap - rem

    return value, flow

def write_fc_max_flow_dot(src, sink):
    value, flow = max_flow(src, sink)
    lines = []
    lines.append("digraph FulfillmentFlow {")
    lines.append("  rankdir=LR;")
    lines.append("  labelloc=t;")
    lines.append(f"  label=\"Ford-Fulkerson (Edmonds-Karp) {src} -> {sink}, max_flow={value}\";")
    lines.append("  node [shape=box, style=filled, fontname=Helvetica, fillcolor=\"#8ecae6\"];")

    for n in fc_nodes:
        border = "#d00000" if n in (src, sink) else "#333333"
        pen = "2.4" if n in (src, sink) else "1.0"
        lines.append(f"  {q(n)} [color=\"{border}\", penwidth={pen}];")

    for u, v, c in fc_edges:
        cap = edge_cap(c)
        f = flow.get((u, v), 0)
        color = "#1d3557" if f > 0 else "#999"
        pen = "2.4" if f > 0 else "1.0"
        style = "solid" if f > 0 else "dashed"
        lines.append(
            f"  {q(u)} -> {q(v)} [label=\"flow {f}/{cap}\", color=\"{color}\", penwidth={pen}, style={style}];"
        )

    lines.append("}")
    write(os.path.join(out_dir, "fulfillment_max_flow.dot"), "\n".join(lines) + "\n")

def hamiltonian_cycle_fc():
    adj = {n: set() for n in fc_nodes}
    for u, v, _ in fc_edges:
        adj[u].add(v)

    def dfs(path, visited, cur, start):
        if len(path) == len(fc_nodes):
            return start in adj[cur]
        for nxt in adj[cur]:
            if nxt in visited:
                continue
            path.append(nxt)
            visited.add(nxt)
            if dfs(path, visited, nxt, start):
                return True
            visited.remove(nxt)
            path.pop()
        return False

    for start in fc_nodes:
        path = [start]
        visited = {start}
        if dfs(path, visited, start, start):
            path.append(start)
            return path
    return []

def write_fc_hamiltonian_dot():
    cycle = hamiltonian_cycle_fc()
    cedges = set()
    for i in range(len(cycle) - 1):
        cedges.add((cycle[i], cycle[i + 1]))

    label = "Hamiltonian circuit found" if cycle else "No Hamiltonian circuit found"

    lines = []
    lines.append("digraph FulfillmentHamiltonian {")
    lines.append("  rankdir=LR;")
    lines.append("  labelloc=t;")
    lines.append(f"  label=\"Kaufmann-Malgrange: {label}\";")
    lines.append("  node [shape=box, style=filled, fontname=Helvetica, fillcolor=\"#8ecae6\"];")

    for n in fc_nodes:
        lines.append(f"  {q(n)};")

    for u, v, _ in fc_edges:
        if (u, v) in cedges:
            lines.append(f"  {q(u)} -> {q(v)} [color=\"#e63946\", penwidth=2.6];")
        else:
            lines.append(f"  {q(u)} -> {q(v)} [color=\"#999\", style=dashed];")

    lines.append("}")
    write(os.path.join(out_dir, "fulfillment_hamiltonian.dot"), "\n".join(lines) + "\n")

write_topology_dot()
write_markets_shortest_paths_dot(market_source)
write_fc_max_flow_dot(fc_source, fc_sink)
write_fc_hamiltonian_dot()

summary = {
    "fixture": fixture_path,
    "out_dir": out_dir,
    "market_source": market_source,
    "fc_source": fc_source,
    "fc_sink": fc_sink,
    "generated_dot": [
        "topology.dot",
        "markets_shortest_paths.dot",
        "fulfillment_max_flow.dot",
        "fulfillment_hamiltonian.dot",
    ],
}

with open(os.path.join(out_dir, "summary.json"), "w", encoding="utf-8") as f:
    json.dump(summary, f, indent=2)

print("[INFO] Generated DOT files:")
for name in summary["generated_dot"]:
    print(f"  - {os.path.join(out_dir, name)}")
print(f"[INFO] Summary: {os.path.join(out_dir, 'summary.json')}")
PY

if command -v dot >/dev/null 2>&1; then
    echo "[INFO] Graphviz found. Rendering PNG..."
    for dot_file in "$OUT_DIR"/*.dot; do
        base="${dot_file%.dot}"
        dot -Tpng "$dot_file" -o "$base.png"
        echo "[INFO] Rendered: $base.png"
    done
else
    echo "[WARN] Graphviz 'dot' not found. Install graphviz to generate PNG."
    echo "[WARN] DOT files were still generated and can be rendered later."
fi

echo "[INFO] Done."
