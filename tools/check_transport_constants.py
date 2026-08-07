#!/usr/bin/env python3
"""Check duplicated F Prime/Teensy transport constants for drift."""

from __future__ import annotations

import ast
import json
import operator
import re
import subprocess
import sys
from pathlib import Path

from generate_transport_constants import resolve_endpoint_profile, resolve_rf_identity


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "config/transport_constants.json"
RF_NETWORKS = ROOT / "config/rf_networks.json"

SOURCES = {
    "fprime": ROOT / "ArtemisRpiTeensy_N2/Components/LinkCfg/LinkCfg.hpp",
    "satellite_teensy": ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp",
    "ground_teensy": ROOT / "GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp",
}

CONST_RE = re.compile(r"static constexpr [^=]+?\b([A-Z][A-Z0-9_]+)\s*=\s*([^;]+);")

OPS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.FloorDiv: operator.floordiv,
    ast.Div: operator.floordiv,
}


def eval_expr(expr: str, values: dict[str, int]) -> int:
    expr = expr.split("//", 1)[0].strip()

    def eval_node(node: ast.AST) -> int:
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return int(node.value)
        if isinstance(node, ast.Name):
            if node.id not in values:
                raise ValueError(f"unknown name {node.id!r}")
            return values[node.id]
        if isinstance(node, ast.BinOp) and type(node.op) in OPS:
            return OPS[type(node.op)](eval_node(node.left), eval_node(node.right))
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return -eval_node(node.operand)
        raise ValueError(f"unsupported expression {expr!r}")

    return eval_node(ast.parse(expr, mode="eval").body)


def read_constants(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for match in CONST_RE.finditer(path.read_text()):
        name, expr = match.groups()
        try:
            values[name] = eval_expr(expr, values)
        except ValueError:
            continue
    return values


def expect_equal(errors: list[str], label: str, *pairs: tuple[str, int]) -> None:
    expected = pairs[0][1]
    for name, value in pairs[1:]:
        if value != expected:
            errors.append(f"{label}: {pairs[0][0]}={expected} but {name}={value}")


def main() -> int:
    generated_check = subprocess.run(
        [sys.executable, str(ROOT / "tools/generate_transport_constants.py"), "--check"],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if generated_check.returncode != 0:
        output = (generated_check.stdout + generated_check.stderr).strip()
        if output:
            print(output)
        print("ERROR: generated transport headers do not match their manifests")
        return 1

    constants = {name: read_constants(path) for name, path in SOURCES.items()}
    manifest = json.loads(MANIFEST.read_text())
    registry = json.loads(RF_NETWORKS.read_text())
    errors: list[str] = []

    fp = constants["fprime"]
    sat = constants["satellite_teensy"]
    gnd = constants["ground_teensy"]
    identity = resolve_rf_identity(manifest, registry)

    common_pairs = [
        ("frame magic 0", ("fprime", fp["UART_FRAME_MAGIC_0"]), ("satellite", sat["FRAME_MAGIC_0"]), ("ground", gnd["FRAME_MAGIC_0"])),
        ("frame magic 1", ("fprime", fp["UART_FRAME_MAGIC_1"]), ("satellite", sat["FRAME_MAGIC_1"]), ("ground", gnd["FRAME_MAGIC_1"])),
        ("channel CCSDS", ("fprime", fp["CHANNEL_CCSDS"]), ("satellite", sat["CHANNEL_CCSDS"]), ("ground", gnd["CHANNEL_CCSDS"])),
        ("channel payload", ("fprime", fp["CHANNEL_PAYLOAD"]), ("satellite", sat["CHANNEL_PAYLOAD"]), ("ground", gnd["CHANNEL_PAYLOAD"])),
        ("UART max payload", ("fprime", fp["UART_FRAME_MAX_PAYLOAD"]), ("satellite", sat["FRAME_MAX_PAYLOAD"]), ("ground", gnd["FRAME_MAX_PAYLOAD"])),
        ("RF payload segment data", ("fprime", fp["RF_SEGMENT_MAX_DATA_BYTES"]), ("satellite", sat["RF_SEGMENT_MAX_DATA"]), ("ground", gnd["RF_SEGMENT_MAX_DATA"])),
        ("default RF network ID", ("registry", identity["network_id"]), ("fprime", fp["RF_NETWORK_ID"])),
        ("RF protocol version", ("registry", identity["protocol_version"]), ("fprime", fp["RF_PROTOCOL_VERSION"])),
        ("default RF ground address", ("registry", identity["ground_address"]), ("fprime", fp["RF_GROUND_ADDRESS"])),
        ("default RF satellite address", ("registry", identity["satellite_address"]), ("fprime", fp["RF_SATELLITE_ADDRESS"])),
        ("RF TX completion timeout", ("manifest", manifest["rf"]["tx_complete_timeout_ms"]), ("fprime", fp["RF_TX_COMPLETE_TIMEOUT_MS"]), ("satellite", sat["RF_TX_COMPLETE_TIMEOUT_MS"]), ("ground", gnd["RF_TX_COMPLETE_TIMEOUT_MS"])),
        ("ground TX CCSDS ACK", ("fprime", fp["RF_GROUND_TX_ACK_REQUIRED_CCSDS"]), ("ground TX", gnd["RF_TX_ACK_REQUIRED_CCSDS"]), ("satellite RX", sat["RF_RX_ACK_REQUIRED_CCSDS"])),
        ("ground TX payload ACK", ("fprime", fp["RF_GROUND_TX_ACK_REQUIRED_PAYLOAD"]), ("ground TX", gnd["RF_TX_ACK_REQUIRED_PAYLOAD"]), ("satellite RX", sat["RF_RX_ACK_REQUIRED_PAYLOAD"])),
        ("satellite TX CCSDS ACK", ("fprime", fp["RF_SATELLITE_TX_ACK_REQUIRED_CCSDS"]), ("satellite TX", sat["RF_TX_ACK_REQUIRED_CCSDS"]), ("ground RX", gnd["RF_RX_ACK_REQUIRED_CCSDS"])),
        ("satellite TX payload ACK", ("fprime", fp["RF_SATELLITE_TX_ACK_REQUIRED_PAYLOAD"]), ("satellite TX", sat["RF_TX_ACK_REQUIRED_PAYLOAD"]), ("ground RX", gnd["RF_RX_ACK_REQUIRED_PAYLOAD"])),
    ]
    for label, *pairs in common_pairs:
        expect_equal(errors, label, *pairs)

    expect_equal(
        errors,
        "satellite local RPC channel",
        ("fprime", fp["CHANNEL_TEENSY_LOCAL"]),
        ("satellite", sat["CHANNEL_TEENSY_LOCAL"]),
    )
    expect_equal(errors, "F Prime payload packet max", ("payload max", fp["PAYLOAD_PACKET_MAX_BYTES"]), ("RF data", fp["RF_SEGMENT_MAX_DATA_BYTES"]))

    if fp["CHANNEL_COUNT"] != 3 or sat["CHANNEL_COUNT"] != 3:
        errors.append("F Prime and satellite Teensy must keep channel 2 for local subsystem RPC")
    if gnd["CHANNEL_COUNT"] != 2:
        errors.append("Ground Teensy should expose only the RF-forwarded channels 0 and 1")
    for name in (
        "RF_GROUND_TX_ACK_REQUIRED_CCSDS",
        "RF_GROUND_TX_ACK_REQUIRED_PAYLOAD",
        "RF_SATELLITE_TX_ACK_REQUIRED_CCSDS",
        "RF_SATELLITE_TX_ACK_REQUIRED_PAYLOAD",
    ):
        if fp[name] != 1:
            errors.append(f"Neutron 2 compatibility requires {name}=1")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print("transport constants OK")
    print(
        f"- default RF pair: {identity['ground_profile']} <-> {identity['spacecraft_profile']}"
    )
    for profile_key in registry["endpoint_profiles"]:
        endpoint = resolve_endpoint_profile(registry, profile_key)
        print(
            f"- RF profile {profile_key}: network=0x{endpoint['network_id']:02X} "
            f"local=0x{endpoint['local_address']:02X} remote=0x{endpoint['remote_address']:02X}"
        )
    for name, path in SOURCES.items():
        print(f"- {name}: {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
