#!/usr/bin/env python3
"""End-to-end order lifecycle simulator for TCP + HTTP graph validation."""

from __future__ import annotations

import argparse
import json
import socket
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


@dataclass
class NodeSpec:
    node_id: str
    kind: str
    latitude: float
    longitude: float
    is_secure: bool
    connections: list[dict[str, Any]]


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def normalize_kind(node_type: str) -> str:
    if node_type in {"hub", "market"}:
        return "hub"
    if node_type in {"warehouse", "fulfillment_center"}:
        return "warehouse"
    raise ValueError(f"Unsupported node type: {node_type}")


def load_nodes_from_map(map_path: Path) -> list[NodeSpec]:
    raw = load_json(map_path)
    if not isinstance(raw, list):
        raise ValueError("Map file must be a JSON array")

    nodes: list[NodeSpec] = []
    for entry in raw:
        node_id = entry["node_id"]
        kind = normalize_kind(entry["node_type"])
        loc = entry.get("node_location", {})
        connections = entry.get("connections", [])
        nodes.append(
            NodeSpec(
                node_id=node_id,
                kind=kind,
                latitude=float(loc.get("latitude", 0.0)),
                longitude=float(loc.get("longitude", 0.0)),
                is_secure=bool(entry.get("is_secure", True)),
                connections=connections,
            )
        )
    return nodes


def client_info_payload(node: NodeSpec) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "type": "client_info",
        "timestamp": utc_now_iso(),
        "is_secure": node.is_secure,
        "location": {"latitude": node.latitude, "longitude": node.longitude},
        "connections": node.connections,
    }
    if node.kind == "hub":
        payload["hub_id"] = node.node_id
    else:
        payload["warehouse_id"] = node.node_id
    return payload


def inventory_update_payload(node_id: str) -> dict[str, Any]:
    return {
        "type": "inventory_update",
        "timestamp": utc_now_iso(),
        "user_id": node_id,
        "inventory": [
            {"item_type": 1, "stock_level": 90, "threshold": 20},
            {"item_type": 2, "stock_level": 80, "threshold": 20},
            {"item_type": 3, "stock_level": 70, "threshold": 20},
        ],
    }


def inventory_update_custom_payload(
    node_id: str, inventory: list[dict[str, int]]
) -> dict[str, Any]:
    """Create custom inventory update with specified stock levels."""
    return {
        "type": "inventory_update",
        "timestamp": utc_now_iso(),
        "user_id": node_id,
        "inventory": inventory,
    }


def restock_notice_payload(
    node_id: str, item_type: int, stock_level: int
) -> dict[str, Any]:
    return {
        "type": "restock_notice",
        "timestamp": utc_now_iso(),
        "user_id": node_id,
        "item_type": item_type,
        "stock_level": stock_level,
    }


def order_request_payload(
    hub_id: str, order_id: str, items_needed: list[dict[str, int]]
) -> dict[str, Any]:
    return {
        "type": "order_request",
        "timestamp": utc_now_iso(),
        "hub_id": hub_id,
        "order_id": order_id,
        "items_needed": items_needed,
    }


def order_status_query_payload(hub_id: str, order_id: str) -> dict[str, Any]:
    return {
        "type": "order_status",
        "timestamp": utc_now_iso(),
        "hub_id": hub_id,
        "order_id": order_id,
    }


def order_dispatch_payload(
    order_id: str, items_shipped: list[dict[str, Any]]
) -> dict[str, Any]:
    return {
        "type": "order_dispatch",
        "timestamp": utc_now_iso(),
        "order_id": order_id,
        "status": "Shipped",
        "items_shipped": items_shipped,
    }


def delivery_update_payload(
    hub_id: str, order_id: str, status: str = "Delivered"
) -> dict[str, Any]:
    return {
        "type": "delivery_update",
        "timestamp": utc_now_iso(),
        "hub_id": hub_id,
        "order_id": order_id,
        "status": status,
    }


def disconnect_payload(node_id: str) -> dict[str, Any]:
    return {
        "type": "disconnect_request",
        "timestamp": utc_now_iso(),
        "user_id": node_id,
    }


def send_json(
    sock: socket.socket, payload: dict[str, Any], recv_timeout_s: float = 1.2
) -> str:
    wire = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    sock.sendall(wire)
    sock.settimeout(recv_timeout_s)
    try:
        data = sock.recv(8192)
        return data.decode("utf-8", errors="replace") if data else ""
    except socket.timeout:
        return ""


def drain_server_push(sock: socket.socket) -> list[dict[str, Any]]:
    messages: list[dict[str, Any]] = []
    sock.settimeout(0.05)
    while True:
        try:
            data = sock.recv(8192)
            if not data:
                break
            text = data.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            try:
                messages.append(json.loads(text))
            except json.JSONDecodeError:
                continue
        except socket.timeout:
            break
        except OSError:
            break
    return messages


def http_post_json(url: str, payload: dict[str, Any]) -> tuple[int, str]:
    request = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=4) as response:
            return response.status, response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", errors="replace")
    except (socket.timeout, TimeoutError, OSError) as exc:
        return 0, f"Timeout or connection error: {exc}"


def run_http_checks(http_cfg: dict[str, Any], map_payload: Any, stage: str) -> None:
    if not http_cfg.get("enabled", False):
        return

    base_url = http_cfg.get("base_url", "http://127.0.0.1:8081").rstrip("/")
    market_source = http_cfg.get("market_source", "H001")
    fc_source = http_cfg.get("fc_source", "W001")
    fc_sink = http_cfg.get("fc_sink", "W005")

    refresh_status, refresh_body = http_post_json(f"{base_url}/map/refresh", {})
    if refresh_status != 200:
        map_status, map_body = http_post_json(f"{base_url}/map", map_payload)
        print(f"[HTTP:{stage}] POST /map/refresh -> {refresh_status}")
        if refresh_body:
            print(f"               {refresh_body[:220]}")
        print(f"[HTTP:{stage}] POST /map -> {map_status}")
        if map_body:
            print(f"               {map_body[:220]}")
    else:
        print(f"[HTTP:{stage}] POST /map/refresh -> {refresh_status}")

    checks = [
        ("markets-path", {"source_id": market_source}),
        ("fulfillment-flow", {"source_id": fc_source, "sink_id": fc_sink}),
        ("fulfillment-circuit", {}),
    ]
    for endpoint, payload in checks:
        status, body = http_post_json(f"{base_url}/{endpoint}", payload)
        print(f"[HTTP:{stage}] POST /{endpoint} -> {status}")
        if body:
            print(f"               {body[:220]}")


def apply_graph_state_changes(
    sockets: dict[str, socket.socket],
    changes_cfg: dict[str, Any],
    http_cfg: dict[str, Any],
    map_payload: Any,
) -> None:
    """Apply graph state changes (empty warehouses, disconnect nodes, etc.)"""
    if not changes_cfg:
        return

    print("\n=== Applying graph state changes ===")

    # HTTP checks before state changes
    if http_cfg.get("enabled", False):
        print("\n--- Graph state before changes ---")
        run_http_checks(http_cfg, map_payload, "pre-state-change")

    # Empty warehouses or set to specific stock levels
    empty_warehouses = changes_cfg.get("empty_warehouses", [])
    for wh_spec in empty_warehouses:
        wh_id = wh_spec if isinstance(wh_spec, str) else wh_spec.get("warehouse_id")
        if not wh_id or wh_id not in sockets:
            print(f"[EMPTY] skip invalid warehouse {wh_id}")
            continue

        # Send inventory update with stock_level = 0
        empty_inventory = [
            {"item_type": 1, "stock_level": 0, "threshold": 20},
            {"item_type": 2, "stock_level": 0, "threshold": 20},
            {"item_type": 3, "stock_level": 0, "threshold": 20},
        ]
        raw = send_json(
            sockets[wh_id], inventory_update_custom_payload(wh_id, empty_inventory)
        )
        print(f"[EMPTY] {wh_id} set all inventory to 0")
        if raw:
            print(f"        {raw}")

    # Disconnect mid-flow warehouses
    disconnect_warehouses = changes_cfg.get("disconnect_warehouses", [])
    for wh_id in disconnect_warehouses:
        if not wh_id or wh_id not in sockets:
            print(f"[DISC-MID] skip invalid warehouse {wh_id}")
            continue
        drain_server_push(sockets[wh_id])
        raw = send_json(sockets[wh_id], disconnect_payload(wh_id))
        print(f"[DISC-MID] {wh_id} disconnected mid-flight")
        if raw:
            print(f"           {raw}")
        del sockets[wh_id]

    # Disconnect mid-flow hubs
    disconnect_hubs = changes_cfg.get("disconnect_hubs", [])
    for hub_id in disconnect_hubs:
        if not hub_id or hub_id not in sockets:
            print(f"[DISC-MID] skip invalid hub {hub_id}")
            continue
        drain_server_push(sockets[hub_id])
        raw = send_json(sockets[hub_id], disconnect_payload(hub_id))
        print(f"[DISC-MID] {hub_id} disconnected mid-flight")
        if raw:
            print(f"           {raw}")
        del sockets[hub_id]

    # Set critical thresholds (high inventory)
    restock_warehouses = changes_cfg.get("restock_to_critical", [])
    for restock_spec in restock_warehouses:
        wh_id = restock_spec.get("warehouse_id")
        stock_level = int(restock_spec.get("stock_level", 200))
        if not wh_id or wh_id not in sockets:
            print(f"[CRIT] skip invalid warehouse {wh_id}")
            continue

        critical_inventory = [
            {"item_type": 1, "stock_level": stock_level, "threshold": 20},
            {"item_type": 2, "stock_level": stock_level, "threshold": 20},
            {"item_type": 3, "stock_level": stock_level, "threshold": 20},
        ]
        raw = send_json(
            sockets[wh_id], inventory_update_custom_payload(wh_id, critical_inventory)
        )
        print(f"[CRIT] {wh_id} set all inventory to {stock_level}")
        if raw:
            print(f"       {raw}")

    time.sleep(1)  # Let database sync

    # HTTP checks after state changes to observe graph refresh
    if http_cfg.get("enabled", False):
        print("\n--- Graph state after changes ---")
        run_http_checks(http_cfg, map_payload, "post-state-change")


def parse_status_response(raw: str) -> str:
    if not raw:
        return "<no-response>"
    try:
        payload = json.loads(raw)
        if isinstance(payload, dict) and "status" in payload:
            return str(payload["status"])
        return "<unknown>"
    except json.JSONDecodeError:
        return "<non-json>"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run full order lifecycle and graph refresh checks"
    )
    parser.add_argument(
        "--scenario",
        default="scripts/scenarios/order_lifecycle_simulation.json",
        help="Path to lifecycle scenario JSON",
    )
    parser.add_argument("--host", help="Override host from scenario")
    parser.add_argument("--port", type=int, help="Override port from scenario")
    parser.add_argument(
        "--no-http-checks", action="store_true", help="Skip HTTP checks"
    )
    parser.add_argument(
        "--no-disconnect", action="store_true", help="Keep sockets open on exit"
    )
    return parser.parse_args()


def resolve_path(workspace_root: Path, maybe_relative: str) -> Path:
    candidate = Path(maybe_relative)
    return candidate if candidate.is_absolute() else workspace_root / candidate


def main() -> int:
    args = parse_args()
    workspace_root = Path(__file__).resolve().parent.parent

    scenario_path = resolve_path(workspace_root, args.scenario)
    scenario = load_json(scenario_path)

    host = args.host or scenario.get("host", "127.0.0.1")
    port = int(args.port or scenario.get("port", 8080))
    connect_delay_s = float(scenario.get("connect_delay_ms", 80)) / 1000.0
    cancel_window_s = int(scenario.get("cancellation_window_seconds", 30))
    post_window_s = int(scenario.get("post_window_buffer_seconds", 8))
    poll_interval_s = int(scenario.get("poll_interval_seconds", 5))

    map_path = resolve_path(workspace_root, scenario["map_path"])
    map_payload = load_json(map_path)
    nodes = load_nodes_from_map(map_path)

    run_http = (
        bool(scenario.get("http_checks", {}).get("enabled", False))
        and not args.no_http_checks
    )
    send_disconnect = (
        bool(scenario.get("send_disconnect_on_exit", True)) and not args.no_disconnect
    )

    sockets: dict[str, socket.socket] = {}

    print(f"Loaded {len(nodes)} nodes from {map_path}")
    print(f"Connecting to TCP server at {host}:{port}\n")

    for node in nodes:
        try:
            sock = socket.create_connection((host, port), timeout=3)
            sockets[node.node_id] = sock
            response = send_json(sock, client_info_payload(node))
            print(f"[AUTH] {node.node_id:>4} ({node.kind})")
            if response:
                print(f"      {response}")
            time.sleep(connect_delay_s)
        except OSError as exc:
            print(f"[AUTH] {node.node_id:>4} failed: {exc}")

    print("\n=== Warehouse inventory sync ===")
    for node in nodes:
        if node.kind != "warehouse" or node.node_id not in sockets:
            continue
        response = send_json(
            sockets[node.node_id], inventory_update_payload(node.node_id)
        )
        print(f"[INV ] {node.node_id} inventory_update")
        if response:
            print(f"      {response}")

    order_cfg = scenario.get("initial_order", {})
    hub_id = order_cfg.get("hub_id")
    if not hub_id:
        hub_id = next(
            (n.node_id for n in nodes if n.kind == "hub" and n.node_id in sockets), None
        )
    if not hub_id or hub_id not in sockets:
        print("No available hub socket for order flow.")
        return 1

    items_needed = order_cfg.get("items_needed", [{"item_type": 1, "quantity": 4}])
    order_id = f"SIM-LIFE-{int(time.time())}"

    print("\n=== Order lifecycle ===")
    response = send_json(
        sockets[hub_id], order_request_payload(hub_id, order_id, items_needed)
    )
    print(f"[ORD ] order_request from {hub_id}, order_id={order_id}")
    if response:
        print(f"      {response}")

    first_status_raw = send_json(
        sockets[hub_id], order_status_query_payload(hub_id, order_id)
    )
    first_status = parse_status_response(first_status_raw)
    print(f"[ORD ] immediate status -> {first_status}")

    print(
        f"\nWaiting {cancel_window_s + post_window_s}s for approval window + processor loop..."
    )
    start = time.time()
    dispatched = False

    while time.time() - start < (cancel_window_s + post_window_s):
        time.sleep(max(1, poll_interval_s))

        status_raw = send_json(
            sockets[hub_id], order_status_query_payload(hub_id, order_id)
        )
        status = parse_status_response(status_raw)
        elapsed = int(time.time() - start)
        print(f"[ORD ] t+{elapsed:02d}s status -> {status}")

        # Server sends supply_request asynchronously to warehouses after approval/request phase.
        for node in nodes:
            if node.kind != "warehouse" or node.node_id not in sockets:
                continue
            pushed = drain_server_push(sockets[node.node_id])
            for msg in pushed:
                if msg.get("type") != "supply_request":
                    continue
                print(
                    f"[PUSH] {node.node_id} received supply_request for {msg.get('order_id')}"
                )
                items = msg.get("items_needed", [])
                dispatch_raw = send_json(
                    sockets[node.node_id],
                    order_dispatch_payload(msg.get("order_id", order_id), items),
                )
                print(f"[ORD ] {node.node_id} -> order_dispatch")
                if dispatch_raw:
                    print(f"      {dispatch_raw}")
                dispatched = True

    if dispatched:
        delivery_raw = send_json(
            sockets[hub_id], delivery_update_payload(hub_id, order_id, "Delivered")
        )
        print(f"[ORD ] {hub_id} -> delivery_update Delivered")
        if delivery_raw:
            print(f"      {delivery_raw}")

        final_raw = send_json(
            sockets[hub_id], order_status_query_payload(hub_id, order_id)
        )
        final_status = parse_status_response(final_raw)
        print(f"[ORD ] final status -> {final_status}")
    else:
        print(
            "[WARN] No supply_request captured; order_dispatch/delivery_update were skipped."
        )

    # Apply graph state changes (empty warehouses, disconnect nodes, etc.)
    apply_graph_state_changes(
        sockets,
        scenario.get("graph_state_changes", {}),
        scenario.get("http_checks", {}),
        map_payload,
    )

    if run_http:
        print("\n=== HTTP post-changes checks ===")
        run_http_checks(scenario.get("http_checks", {}), map_payload, "post-restock")

    if send_disconnect:
        print("\n=== Disconnecting clients ===")
        for node_id, sock in sockets.items():
            # Drain any buffered messages before disconnect to avoid reading stale responses
            drain_server_push(sock)
            raw = send_json(sock, disconnect_payload(node_id))
            print(f"[DISC] {node_id}")
            if raw:
                print(f"      {raw}")

    for sock in sockets.values():
        try:
            sock.close()
        except OSError:
            pass

    print("\nLifecycle simulation finished.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
        raise SystemExit(130)
