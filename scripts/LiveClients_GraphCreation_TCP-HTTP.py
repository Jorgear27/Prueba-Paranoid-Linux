#!/usr/bin/env python3
"""Simulate multiple hubs and warehouses connecting to the TCP server."""

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


def send_json(
    sock: socket.socket, payload: dict[str, Any], recv_timeout_s: float = 1.5
) -> str:
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    sock.sendall(data)
    sock.settimeout(recv_timeout_s)
    try:
        response = sock.recv(8192)
        return response.decode("utf-8", errors="replace") if response else ""
    except socket.timeout:
        return ""


def warehouse_inventory_payload(node_id: str) -> dict[str, Any]:
    return {
        "type": "inventory_update",
        "timestamp": utc_now_iso(),
        "user_id": node_id,
        "inventory": [
            {"item_type": 1, "stock_level": 100, "threshold": 20},
            {"item_type": 2, "stock_level": 100, "threshold": 20},
            {"item_type": 3, "stock_level": 100, "threshold": 20},
        ],
    }


def restock_notice_payload(node_id: str, item_type: int = 2) -> dict[str, Any]:
    return {
        "type": "restock_notice",
        "timestamp": utc_now_iso(),
        "user_id": node_id,
        "item_type": item_type,
        "stock_level": 100,
    }


def order_request_payload(hub_id: str, order_id: str) -> dict[str, Any]:
    return {
        "type": "order_request",
        "timestamp": utc_now_iso(),
        "hub_id": hub_id,
        "order_id": order_id,
        "items_needed": [
            {"item_type": 1, "quantity": 5},
            {"item_type": 2, "quantity": 3},
        ],
    }


def order_status_query_payload(hub_id: str, order_id: str) -> dict[str, Any]:
    return {
        "type": "order_status",
        "timestamp": utc_now_iso(),
        "hub_id": hub_id,
        "order_id": order_id,
    }


def disconnect_payload(node_id: str) -> dict[str, Any]:
    return {
        "type": "disconnect_request",
        "timestamp": utc_now_iso(),
        "user_id": node_id,
    }


def http_post_json(url: str, payload: dict[str, Any]) -> tuple[int, str]:
    request = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=4) as response:
            body = response.read().decode("utf-8", errors="replace")
            return response.status, body
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        return exc.code, body


def run_http_checks(http_cfg: dict[str, Any], map_payload: Any) -> None:
    if not http_cfg.get("enabled", False):
        return

    base_url = http_cfg.get("base_url", "http://127.0.0.1:8081").rstrip("/")
    market_source = http_cfg.get("market_source", "H001")
    fc_source = http_cfg.get("fc_source", "W001")
    fc_sink = http_cfg.get("fc_sink", "W005")

    checks = [
        ("markets-path", {"source_id": market_source}),
        ("fulfillment-flow", {"source_id": fc_source, "sink_id": fc_sink}),
        ("fulfillment-circuit", {}),
    ]

    # Ensure the in-memory graph exists before running algorithms.
    refresh_status, refresh_body = http_post_json(f"{base_url}/map/refresh", {})
    if refresh_status != 200:
        map_status, map_body = http_post_json(f"{base_url}/map", map_payload)
        print(f"[HTTP] POST /map/refresh -> {refresh_status}")
        if refresh_body:
            print(f"       {refresh_body[:220]}")
        print(f"[HTTP] POST /map -> {map_status}")
        if map_body:
            print(f"       {map_body[:220]}")
    else:
        print(f"[HTTP] POST /map/refresh -> {refresh_status}")
        if refresh_body:
            print(f"       {refresh_body[:220]}")

    print("\n=== HTTP algorithm checks ===")
    for endpoint, payload in checks:
        status, body = http_post_json(f"{base_url}/{endpoint}", payload)
        print(f"[HTTP] POST /{endpoint} -> {status}")
        if body:
            print(f"       {body[:220]}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Simulate many hubs and warehouses against the server"
    )
    parser.add_argument(
        "--scenario",
        default="scripts/scenarios/multi_clients_simulation.json",
        help="Path to simulation scenario JSON",
    )
    parser.add_argument("--host", help="Override host from scenario")
    parser.add_argument("--port", type=int, help="Override port from scenario")
    parser.add_argument(
        "--no-core-flows",
        action="store_true",
        help="Skip inventory/order flow messages",
    )
    parser.add_argument(
        "--no-disconnect",
        action="store_true",
        help="Do not send disconnect requests at the end",
    )
    parser.add_argument(
        "--no-http-checks", action="store_true", help="Skip HTTP checks at the end"
    )
    return parser.parse_args()


def resolve_path(workspace_root: Path, maybe_relative: str) -> Path:
    candidate = Path(maybe_relative)
    if candidate.is_absolute():
        return candidate
    return workspace_root / candidate


def main() -> int:
    args = parse_args()
    workspace_root = Path(__file__).resolve().parent.parent

    scenario_path = resolve_path(workspace_root, args.scenario)
    scenario = load_json(scenario_path)

    host = args.host or scenario.get("host", "127.0.0.1")
    port = int(args.port or scenario.get("port", 8080))
    connect_delay_s = float(scenario.get("connect_delay_ms", 100)) / 1000.0

    exercise_core = (
        bool(scenario.get("exercise_core_flows", True)) and not args.no_core_flows
    )
    send_disconnect = (
        bool(scenario.get("send_disconnect_on_exit", True)) and not args.no_disconnect
    )
    run_http = (
        bool(scenario.get("http_checks", {}).get("enabled", False))
        and not args.no_http_checks
    )

    map_path = resolve_path(workspace_root, scenario["map_path"])
    nodes = load_nodes_from_map(map_path)

    sockets: dict[str, socket.socket] = {}
    print(f"Loaded {len(nodes)} nodes from {map_path}")
    print(f"Connecting to server at {host}:{port}\n")

    for node in nodes:
        try:
            sock = socket.create_connection((host, port), timeout=3)
            sockets[node.node_id] = sock
            response = send_json(sock, client_info_payload(node))
            print(f"[AUTH] {node.node_id:>4} ({node.kind}) -> connected")
            if response:
                print(f"      response: {response}")
            time.sleep(connect_delay_s)
        except OSError as exc:
            print(f"[AUTH] {node.node_id:>4} ({node.kind}) -> failed: {exc}")

    if exercise_core:
        print("\n=== Core flow exercise ===")

        for node in nodes:
            if node.kind != "warehouse" or node.node_id not in sockets:
                continue
            response = send_json(
                sockets[node.node_id], warehouse_inventory_payload(node.node_id)
            )
            print(f"[FLOW] inventory_update from {node.node_id}")
            if response:
                print(f"       response: {response}")

        first_wh = next(
            (
                n.node_id
                for n in nodes
                if n.kind == "warehouse" and n.node_id in sockets
            ),
            None,
        )
        """
        if first_wh:
            response = send_json(sockets[first_wh], restock_notice_payload(first_wh))
            print(f"[FLOW] restock_notice from {first_wh}")
            if response:
                print(f"       response: {response}")

        first_hub = next(
            (n.node_id for n in nodes if n.kind == "hub" and n.node_id in sockets), None
        )
        if first_hub:
            order_id = f"SIM-{int(time.time())}"
            response = send_json(
                sockets[first_hub], order_request_payload(first_hub, order_id)
            )
            print(f"[FLOW] order_request from {first_hub}, order_id={order_id}")
            if response:
                print(f"       response: {response}")

            response = send_json(
                sockets[first_hub], order_status_query_payload(first_hub, order_id)
            )
            print(f"[FLOW] order_status_query from {first_hub}, order_id={order_id}")
            if response:
                print(f"       response: {response}")
        """
    interrupted = False
    print("\nSockets are open. Press Ctrl+C to stop.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        interrupted = True
        print("\nInterrupted by user. Shutting down...")

    if send_disconnect:
        print("\n=== Graceful disconnect ===")
        for node_id, sock in sockets.items():
            try:
                response = send_json(sock, disconnect_payload(node_id))
                print(f"[DISC] {node_id} -> disconnect request sent")
                if response:
                    print(f"      response: {response}")
            except OSError as exc:
                print(f"[DISC] {node_id} -> disconnect failed: {exc}")

    for sock in sockets.values():
        try:
            sock.close()
        except OSError:
            pass

    if run_http:
        run_http_checks(scenario.get("http_checks", {}), load_json(map_path))

    print("\nSimulation finished.")
    return 130 if interrupted else 0


if __name__ == "__main__":
    raise SystemExit(main())
