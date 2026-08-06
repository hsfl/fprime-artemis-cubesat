#!/usr/bin/env python3
"""Collect read-only HackRF/F Prime HIL evidence into an atomic proof summary."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
import re
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Iterable

from rf_safety import (
    BASELINE_RF_PATH_LABEL,
    BASELINE_RX_LNA_GAIN_DB,
    BASELINE_RX_VGA_GAIN_DB,
    BASELINE_TX_GAIN_DB,
    BASELINE_TX_LEADING_MS,
    BASELINE_TX_MODE,
)
from rf_autocal import RX_CANDIDATES, TX_CANDIDATES


SCHEMA_VERSION = 1
NOMINAL_TRANSFER_TARGET_S = 75.0
LIVE_DEMO_CUTOFF_S = 120.0
FAILURE_RESULTS = {
    "crc_failed",
    "decode_failed",
    "error",
    "failed",
    "partial",
    "partial_decode_failed",
    "timed_out",
    "timeout",
}


def _is_event(name: str, suffix: str) -> bool:
    lowered = name.casefold()
    suffix_lowered = suffix.casefold()
    return lowered == suffix_lowered or lowered.endswith("." + suffix_lowered)


def _base_mode(description: str) -> bool:
    return re.search(r"\bBASE\b", description, flags=re.IGNORECASE) is not None


EventPredicate = Callable[[str], bool]
EVENT_GATES: tuple[tuple[str, str, bool, EventPredicate | None], ...] = (
    ("ping_command", "missionApp.Pong", True, None),
    ("base_mode", "missionApp.ModeChanged", True, _base_mode),
    ("soh_snapshot", "sohApp.Snapshot", True, None),
    ("collection_scheduled", "missionApp.CollectionScheduled", True, None),
    ("collection_triggered", "scienceApp.CollectionTriggered", True, None),
    ("science_product_ready", "scienceApp.ScienceProductReady", True, None),
    ("science_stored", "storageManager.ScienceStored", True, None),
    ("downlink_requested", "commsApp.DownlinkRequested", True, None),
    # Current C3M storage hands the fresh descriptor directly to the downlink
    # path; this event exists only when the optional storage-report path runs.
    ("downlink_prepared", "storageManager.DownlinkPrepared", False, None),
    (
        "payload_downlink_started",
        "payloadDownlinkApp.PayloadDownlinkStarted",
        True,
        None,
    ),
    (
        "payload_downlink_complete",
        "payloadDownlinkApp.PayloadDownlinkComplete",
        True,
        None,
    ),
    ("downlink_finished", "commsApp.DownlinkFinished", True, None),
    (
        "capture_duration_configured",
        "scienceApp.CaptureDurationConfigured",
        False,
        None,
    ),
    ("latest_dataset_reported", "storageManager.LatestDataset", False, None),
)

FAILURE_EVENT_SUFFIXES = (
    "commsApp.CommsCommandRejected",
    "commsApp.DownlinkFailed",
    "payloadDownlinkApp.PayloadDownlinkFailed",
    "payloadDriverLepton.ImageCaptureFailed",
    "payloadDriverNeutronSim.CaptureFailed",
)


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _atomic_write_text(path: Path, value: str) -> None:
    """Replace one file atomically without leaving a partial proof artifact."""

    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent, prefix=f".{path.name}.", suffix=".tmp"
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
            stream.write(value)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _integer(value: object) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    return None


def _number(value: object) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return float(value)


def _mapping(value: object) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _counter(mapping: dict[str, Any], key: str) -> int | None:
    return _integer(mapping.get(key))


def _add_failure(failures: list[str], message: str) -> None:
    if message not in failures:
        failures.append(message)


def _read_json(
    path: Path,
    label: str,
    sources: dict[str, dict[str, Any]],
    failures: list[str],
) -> dict[str, Any] | None:
    source: dict[str, Any] = {"path": str(path), "present": path.is_file()}
    sources[label] = source
    if not path.is_file():
        source.update({"valid": False, "error": "missing"})
        _add_failure(failures, f"missing required artifact: {label}")
        return None
    try:
        source["size_bytes"] = path.stat().st_size
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        source.update({"valid": False, "error": str(exc)})
        _add_failure(failures, f"invalid required artifact: {label}")
        return None
    if not isinstance(value, dict):
        source.update({"valid": False, "error": "root is not a JSON object"})
        _add_failure(failures, f"invalid required artifact: {label}")
        return None
    source["valid"] = True
    return value


def _read_event_log(
    path: Path,
    sources: dict[str, dict[str, Any]],
    failures: list[str],
) -> tuple[str, list[dict[str, Any]]]:
    source: dict[str, Any] = {"path": str(path), "present": path.is_file()}
    sources["gds_event_log"] = source
    if not path.is_file():
        source.update({"valid": False, "error": "missing"})
        _add_failure(failures, "missing required artifact: gds_event_log")
        return "", []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
        source["size_bytes"] = path.stat().st_size
    except OSError as exc:
        source.update({"valid": False, "error": str(exc)})
        _add_failure(failures, "invalid required artifact: gds_event_log")
        return "", []
    if not text.strip():
        source.update({"valid": False, "error": "empty"})
        _add_failure(failures, "invalid required artifact: gds_event_log")
        return text, []

    records: list[dict[str, Any]] = []
    malformed = 0
    for line_number, row in enumerate(csv.reader(io.StringIO(text)), start=1):
        if len(row) < 3:
            malformed += 1
            continue
        records.append(
            {
                "line": line_number,
                "timestamp": row[0].strip(),
                "name": row[2].strip(),
                "description": ",".join(row[5:]).strip() if len(row) > 5 else "",
            }
        )
    source.update(
        {
            "valid": bool(records),
            "records": len(records),
            "malformed_lines": malformed,
        }
    )
    if not records:
        source["error"] = "no parseable event records"
        _add_failure(failures, "invalid required artifact: gds_event_log")
    return text, records


def _event_matches(
    records: Iterable[dict[str, Any]],
    suffix: str,
    predicate: EventPredicate | None = None,
) -> list[dict[str, Any]]:
    matches = []
    for record in records:
        if not _is_event(str(record.get("name", "")), suffix):
            continue
        description = str(record.get("description", ""))
        if predicate is None or predicate(description):
            matches.append(record)
    return matches


def _event_reference(record: dict[str, Any]) -> dict[str, Any]:
    return {
        "line": record.get("line"),
        "timestamp": record.get("timestamp"),
        "name": record.get("name"),
        "description": record.get("description"),
    }


def _resolve_payload_file(payload_run: Path, payload: dict[str, Any], override: Path | None) -> Path | None:
    candidates: list[Path] = []
    if override is not None:
        candidates.append(override.expanduser())
    else:
        outputs = _mapping(payload.get("outputs"))
        fdp_value = outputs.get("fdp")
        decode_input = _mapping(payload.get("decode")).get("input")
        for value in (fdp_value, decode_input):
            if not isinstance(value, str) or not value.strip():
                continue
            candidate = Path(value).expanduser()
            if candidate.is_absolute():
                candidates.append(candidate)
            else:
                candidates.append(payload_run.parent / candidate)
            candidates.append(payload_run.parent / candidate.name)
        candidates.append(payload_run.parent / "payload.fdp")

    seen: set[str] = set()
    for candidate in candidates:
        resolved = candidate.resolve(strict=False)
        key = str(resolved)
        if key in seen:
            continue
        seen.add(key)
        if resolved.is_file():
            return resolved
    return None


def _payload_failed_state(payload: dict[str, Any]) -> str | None:
    for key in ("result", "status", "state"):
        value = payload.get(key)
        if isinstance(value, str) and value.casefold() in FAILURE_RESULTS:
            return f"{key}={value}"
    decode = payload.get("decode")
    if isinstance(decode, dict):
        for key in ("result", "status", "state"):
            value = decode.get(key)
            if isinstance(value, str) and value.casefold() in FAILURE_RESULTS:
                return f"decode.{key}={value}"
    return None


def _timing_band(elapsed_s: float | None) -> str | None:
    if elapsed_s is None or elapsed_s < 0:
        return None
    if elapsed_s <= NOMINAL_TRANSFER_TARGET_S:
        return "nominal"
    if elapsed_s < LIVE_DEMO_CUTOFF_S:
        return "degraded"
    return "delayed"


def _event_payload_consistency(
    records: list[dict[str, Any]], payload: dict[str, Any]
) -> dict[str, Any]:
    product_id = _integer(payload.get("product_id"))
    transfer_id = _integer(payload.get("transfer_id"))
    total_bytes = _integer(payload.get("total_bytes"))
    total_packets = _integer(payload.get("total_packets"))

    started_pattern = re.compile(
        r"product=(\d+)\s+bytes=(\d+)\s+packets=(\d+)", re.IGNORECASE
    )
    complete_pattern = re.compile(
        r"transfer=(\d+)\s+packetsSent=(\d+)", re.IGNORECASE
    )
    finished_pattern = re.compile(r"(?:for\s+)?(\d+)\s+bytes", re.IGNORECASE)

    checks: dict[str, Any] = {}
    specifications = (
        (
            "started_matches_payload",
            "payloadDownlinkApp.PayloadDownlinkStarted",
            started_pattern,
            (product_id, total_bytes, total_packets),
        ),
        (
            "complete_matches_payload",
            "payloadDownlinkApp.PayloadDownlinkComplete",
            complete_pattern,
            (transfer_id, total_packets),
        ),
        (
            "finished_bytes_match_payload",
            "commsApp.DownlinkFinished",
            finished_pattern,
            (total_bytes,),
        ),
    )
    for key, suffix, pattern, expected in specifications:
        matched_record: dict[str, Any] | None = None
        parsed_values: tuple[int, ...] | None = None
        latest_values: tuple[int, ...] | None = None
        for record in _event_matches(records, suffix):
            match = pattern.search(str(record.get("description", "")))
            if match is None:
                continue
            values = tuple(int(value) for value in match.groups())
            latest_values = values
            if all(value is not None for value in expected) and values == expected:
                matched_record = record
                parsed_values = values
        checks[key] = {
            "passed": matched_record is not None,
            "expected": list(expected),
            "observed": list(parsed_values or latest_values)
            if (parsed_values or latest_values) is not None
            else None,
            "event": _event_reference(matched_record) if matched_record else None,
        }
    return checks


def _description_values(
    record: dict[str, Any], pattern: re.Pattern[str]
) -> tuple[int, ...] | None:
    match = pattern.search(str(record.get("description", "")))
    if match is None:
        return None
    return tuple(int(value) for value in match.groups())


def _latest_before(
    records: list[dict[str, Any]],
    suffix: str,
    before_line: int,
    predicate: EventPredicate | None = None,
) -> dict[str, Any] | None:
    matches = [
        record
        for record in _event_matches(records, suffix, predicate)
        if int(record.get("line", -1)) < before_line
    ]
    return matches[-1] if matches else None


def _first_after(
    records: list[dict[str, Any]],
    suffix: str,
    after_line: int,
    *,
    before_line: int | None = None,
    predicate: EventPredicate | None = None,
) -> dict[str, Any] | None:
    for record in _event_matches(records, suffix, predicate):
        line = int(record.get("line", -1))
        if line <= after_line or (before_line is not None and line >= before_line):
            continue
        return record
    return None


def _ordered_demo_chain(
    records: list[dict[str, Any]], payload: dict[str, Any]
) -> dict[str, Any]:
    """Find one exact, chronological Base-to-payload event chain."""

    product_id = _integer(payload.get("product_id"))
    transfer_id = _integer(payload.get("transfer_id"))
    total_bytes = _integer(payload.get("total_bytes"))
    total_packets = _integer(payload.get("total_packets"))
    expected = (product_id, transfer_id, total_bytes, total_packets)
    if any(value is None for value in expected):
        return {
            "passed": False,
            "detail": f"payload identity is incomplete: {expected!r}",
            "events": {},
        }

    started_pattern = re.compile(
        r"product=(\d+)\s+bytes=(\d+)\s+packets=(\d+)", re.IGNORECASE
    )
    complete_pattern = re.compile(
        r"transfer=(\d+)\s+packetsSent=(\d+)", re.IGNORECASE
    )
    finished_pattern = re.compile(r"(?:for\s+)?(\d+)\s+bytes", re.IGNORECASE)
    size_pattern = re.compile(r"(?:size|bytes)=(\d+)", re.IGNORECASE)
    request_pattern = re.compile(r"(?:for\s+)?(\d+)\s+bytes", re.IGNORECASE)
    ten_second_pattern = re.compile(r"(?:in|delay=)\s*10\s*s", re.IGNORECASE)

    target_started = (product_id, total_bytes, total_packets)
    target_complete = (transfer_id, total_packets)
    target_bytes = (total_bytes,)
    started_candidates = [
        record
        for record in _event_matches(
            records, "payloadDownlinkApp.PayloadDownlinkStarted"
        )
        if _description_values(record, started_pattern) == target_started
    ]

    for started in reversed(started_candidates):
        started_line = int(started["line"])
        complete = _first_after(
            records,
            "payloadDownlinkApp.PayloadDownlinkComplete",
            started_line,
            predicate=lambda description: (
                (match := complete_pattern.search(description)) is not None
                and tuple(int(value) for value in match.groups()) == target_complete
            ),
        )
        if complete is None:
            continue
        finished = _first_after(
            records,
            "commsApp.DownlinkFinished",
            int(complete["line"]),
            predicate=lambda description: (
                (match := finished_pattern.search(description)) is not None
                and tuple(int(value) for value in match.groups()) == target_bytes
            ),
        )
        if finished is None:
            continue

        requested = _latest_before(
            records,
            "commsApp.DownlinkRequested",
            started_line,
            predicate=lambda description: (
                (match := request_pattern.search(description)) is not None
                and tuple(int(value) for value in match.groups()) == target_bytes
            ),
        )
        if requested is None:
            continue
        prepared = _first_after(
            records,
            "storageManager.DownlinkPrepared",
            int(requested["line"]),
            before_line=started_line,
            predicate=lambda description: (
                (match := size_pattern.search(description)) is not None
                and tuple(int(value) for value in match.groups()) == target_bytes
            ),
        )
        stored = _latest_before(
            records,
            "storageManager.ScienceStored",
            int(requested["line"]),
            predicate=lambda description: (
                (match := size_pattern.search(description)) is not None
                and tuple(int(value) for value in match.groups()) == target_bytes
            ),
        )
        if stored is None:
            continue
        ready = _latest_before(
            records,
            "scienceApp.ScienceProductReady",
            int(stored["line"]),
            predicate=lambda description: (
                (match := size_pattern.search(description)) is not None
                and tuple(int(value) for value in match.groups()) == target_bytes
            ),
        )
        if ready is None:
            continue
        triggered = _latest_before(
            records,
            "scienceApp.CollectionTriggered",
            int(ready["line"]),
            predicate=lambda description: ten_second_pattern.search(description) is not None,
        )
        if triggered is None:
            continue
        scheduled = _latest_before(
            records,
            "missionApp.CollectionScheduled",
            int(triggered["line"]),
            predicate=lambda description: ten_second_pattern.search(description) is not None,
        )
        if scheduled is None:
            continue
        snapshot = _latest_before(
            records, "sohApp.Snapshot", int(scheduled["line"])
        )
        if snapshot is None:
            continue
        base = _latest_before(
            records,
            "missionApp.ModeChanged",
            int(snapshot["line"]),
            _base_mode,
        )
        if base is None:
            continue

        ordered = {
            "base_mode": base,
            "soh_snapshot": snapshot,
            "collection_scheduled": scheduled,
            "collection_triggered": triggered,
            "science_product_ready": ready,
            "science_stored": stored,
            "downlink_requested": requested,
            "payload_downlink_started": started,
            "payload_downlink_complete": complete,
            "downlink_finished": finished,
        }
        return {
            "passed": True,
            "detail": " < ".join(ordered),
            "events": {
                key: _event_reference(record) for key, record in ordered.items()
            },
        }

    return {
        "passed": False,
        "detail": (
            "no exact chronological Base -> SOH -> 10 s collection -> matching "
            f"product {product_id}/transfer {transfer_id} downlink chain"
        ),
        "events": {},
    }


def _gate(
    gates: dict[str, dict[str, Any]],
    failures: list[str],
    key: str,
    passed: bool,
    detail: str,
    *,
    required: bool = True,
) -> None:
    gates[key] = {"passed": bool(passed), "required": required, "detail": detail}
    if required and not passed:
        _add_failure(failures, f"gate failed: {key}: {detail}")


def collect_proof(
    supervisor_run: Path,
    payload_run: Path,
    *,
    event_log: Path | None = None,
    payload_file: Path | None = None,
    pi_sha256: str | None = None,
    pi_path: str | None = None,
) -> dict[str, Any]:
    """Collect evidence without opening a radio, serial port, network, or SSH session."""

    supervisor_run = supervisor_run.resolve()
    payload_run = payload_run.resolve()
    event_log = (event_log or supervisor_run / "gds" / "event.log").resolve()
    failures: list[str] = []
    warnings: list[str] = []
    sources: dict[str, dict[str, Any]] = {}
    gates: dict[str, dict[str, Any]] = {}

    manifest = _read_json(
        supervisor_run / "run-manifest.json", "run_manifest", sources, failures
    )
    bridge = _read_json(
        supervisor_run / "bridge-status.json", "bridge_status", sources, failures
    )
    payload = _read_json(payload_run, "payload_run", sources, failures)
    _, event_records = _read_event_log(event_log, sources, failures)

    supervisor_summary: dict[str, Any] = {}
    if manifest is not None:
        state = str(manifest.get("state", ""))
        supervisor_summary = {
            "schema": manifest.get("schema"),
            "state": state,
            "network": manifest.get("network"),
            "hackrf_serial": manifest.get("hackrf_serial"),
            "tx_enabled": manifest.get("tx_enabled"),
            "tx_mode": manifest.get("tx_mode"),
            "tx_gain": manifest.get("tx_gain"),
            "tx_leading_ms": manifest.get("tx_leading_ms"),
            "rx_lna_gain": manifest.get("rx_lna_gain"),
            "rx_vga_gain": manifest.get("rx_vga_gain"),
            "gain_control": manifest.get("gain_control"),
            "rf_path_label": manifest.get("rf_path_label"),
            "tx_safety_confirmed": manifest.get("tx_safety_confirmed"),
            "elevated_tx_gain_confirmed": manifest.get(
                "elevated_tx_gain_confirmed"
            ),
            "rf_amp_enabled": manifest.get("rf_amp_enabled"),
            "antenna_power_enabled": manifest.get("antenna_power_enabled"),
            "git_branch": manifest.get("git_branch"),
            "git_commit": manifest.get("git_commit"),
            "started_at_s": manifest.get("started_at_s"),
            "updated_at_s": manifest.get("updated_at_s"),
            "children": manifest.get("children"),
        }
        _gate(
            gates,
            failures,
            "supervisor_state",
            state in {"ready", "stopping", "stopped"},
            f"state={state or 'missing'}",
        )
        _gate(
            gates,
            failures,
            "supervisor_schema",
            manifest.get("schema") == 1,
            f"schema={manifest.get('schema')!r}",
        )
        if state == "ready":
            warnings.append("proof was collected while the supervisor was still running")

    bridge_summary: dict[str, Any] = {}
    if bridge is not None:
        messages = _mapping(bridge.get("messages"))
        message_bytes = _mapping(bridge.get("message_bytes"))
        tx_messages = _mapping(bridge.get("tx_messages"))
        reassembly = _mapping(bridge.get("reassembly"))
        tx_queue = _mapping(bridge.get("tx_queue"))
        pty = _mapping(bridge.get("pty"))
        pty_backlogs = {
            channel: _counter(_mapping(value), "backlog_bytes")
            for channel, value in pty.items()
        }
        ack = {
            "tx_mode": bridge.get("tx_mode"),
            "tx_messages_channel_0": _counter(tx_messages, "0"),
            "tx_segments": _counter(bridge, "tx_segments"),
            "tx_attempts": _counter(bridge, "tx_attempts"),
            "ack_received": _counter(bridge, "ack_received"),
            "ack_timeouts": _counter(bridge, "ack_timeouts"),
            "degraded_segments": _counter(bridge, "degraded_segments"),
            "tx_failures": _counter(bridge, "tx_failures"),
        }
        drops = {
            "hackrf_rx_dropped_blocks": _counter(bridge, "hackrf_rx_dropped_blocks"),
            "reassembly_drops": _counter(reassembly, "drops"),
            "reassembly_timeouts": _counter(reassembly, "timeouts"),
            "reassembly_duplicates": _counter(reassembly, "duplicates"),
            "tx_queue_rejections": _counter(tx_queue, "rejections"),
        }
        bridge_log = supervisor_run / "bridge.log"
        backpressure_lines: list[str] = []
        if bridge_log.is_file():
            try:
                for line in bridge_log.read_text(
                    encoding="utf-8", errors="replace"
                ).splitlines():
                    if "backpressure" in line.casefold() or "backlog would exceed" in line.casefold():
                        backpressure_lines.append(line.strip())
            except OSError:
                pass
        last_error = bridge.get("last_error")
        backlog_nonzero = any(value not in {None, 0} for value in pty_backlogs.values())
        backpressure_detected = (
            backlog_nonzero
            or (drops["tx_queue_rejections"] not in {None, 0})
            or bool(backpressure_lines)
            or (
                isinstance(last_error, str)
                and ("backpressure" in last_error.casefold() or "backlog" in last_error.casefold())
            )
        )
        bridge_summary = {
            "schema": bridge.get("schema"),
            "radio_state": bridge.get("radio_state"),
            "hackrf_mode": bridge.get("hackrf_mode"),
            "network": bridge.get("network"),
            "network_id": bridge.get("network_id"),
            "serial": bridge.get("serial"),
            "rf_contract": bridge.get("rf_contract"),
            "tx_enabled": bridge.get("tx_enabled"),
            "tx_gain": bridge.get("tx_gain"),
            "tx_leading_ms": bridge.get("tx_leading_ms"),
            "rx_lna_gain": bridge.get("rx_lna_gain"),
            "rx_vga_gain": bridge.get("rx_vga_gain"),
            "gain_control": bridge.get("gain_control"),
            "rf_path_label": bridge.get("rf_path_label"),
            "tx_safety_confirmed": bridge.get("tx_safety_confirmed"),
            "elevated_tx_gain_confirmed": bridge.get(
                "elevated_tx_gain_confirmed"
            ),
            "rf_amp_enabled": bridge.get("rf_amp_enabled"),
            "antenna_power_enabled": bridge.get("antenna_power_enabled"),
            "rx_blocks": bridge.get("rx_blocks"),
            "rx_bytes": bridge.get("rx_bytes"),
            "rx_iq": _mapping(bridge.get("rx_iq")),
            "rf22_frames": bridge.get("rf22_frames"),
            "channels": {
                "0": {
                    "messages": _counter(messages, "0"),
                    "message_bytes": _counter(message_bytes, "0"),
                    "pty": _mapping(pty.get("0")),
                },
                "1": {
                    "messages": _counter(messages, "1"),
                    "message_bytes": _counter(message_bytes, "1"),
                    "pty": _mapping(pty.get("1")),
                },
            },
            "command_ack": ack,
            "drops": drops,
            "backpressure": {
                "detected": backpressure_detected,
                "pty_backlog_bytes": pty_backlogs,
                "bridge_log_matches": len(backpressure_lines),
                "last_matches": backpressure_lines[-3:],
            },
            "tx_queue": tx_queue,
            "reconnects": bridge.get("reconnects"),
            "last_error": last_error,
        }

        radio_state = str(bridge.get("radio_state", ""))
        _gate(
            gates,
            failures,
            "bridge_state",
            radio_state in {"receiving", "stopped"},
            f"radio_state={radio_state or 'missing'}",
        )
        _gate(
            gates,
            failures,
            "bridge_schema",
            bridge.get("schema") == 1,
            f"schema={bridge.get('schema')!r}",
        )
        if manifest is not None:
            same_network = (
                bool(manifest.get("network"))
                and manifest.get("network") == bridge.get("network")
            )
            _gate(
                gates,
                failures,
                "rf_network_consistent",
                same_network,
                f"manifest={manifest.get('network')!r}, bridge={bridge.get('network')!r}",
            )

            provenance_present = any(
                key in manifest or key in bridge
                for key in (
                    "rf_path_label",
                    "tx_gain",
                    "rx_lna_gain",
                    "rx_vga_gain",
                    "rf_amp_enabled",
                    "antenna_power_enabled",
                )
            )
            if provenance_present:
                manifest_tx_gain = _integer(manifest.get("tx_gain"))
                bridge_tx_gain = _integer(bridge.get("tx_gain"))
                manifest_rx_lna = _integer(manifest.get("rx_lna_gain"))
                bridge_rx_lna = _integer(bridge.get("rx_lna_gain"))
                manifest_rx_vga = _integer(manifest.get("rx_vga_gain"))
                bridge_rx_vga = _integer(bridge.get("rx_vga_gain"))
                manifest_tx_enabled = manifest.get("tx_enabled")
                bridge_tx_enabled = bridge.get("tx_enabled")
                manifest_label = manifest.get("rf_path_label")
                bridge_label = bridge.get("rf_path_label")
                manifest_rx_amp = manifest.get("rx_rf_amp_enabled")
                bridge_rx_amp = bridge.get("rx_rf_amp_enabled")
                manifest_tx_amp = manifest.get("tx_rf_amp_enabled")
                bridge_tx_amp = bridge.get("tx_rf_amp_enabled")
                directional_amp_match = (
                    isinstance(manifest_rx_amp, bool)
                    and manifest_rx_amp == bridge_rx_amp
                    and isinstance(manifest_tx_amp, bool)
                    and manifest_tx_amp == bridge_tx_amp
                    and manifest.get("rf_amp_enabled")
                    == (manifest_rx_amp or manifest_tx_amp)
                    and bridge.get("rf_amp_enabled")
                    == (bridge_rx_amp or bridge_tx_amp)
                )
                gains_match = (
                    manifest_tx_gain is not None
                    and manifest_tx_gain >= 0
                    and manifest_tx_gain == bridge_tx_gain
                    and manifest_rx_lna is not None
                    and manifest_rx_lna >= 0
                    and manifest_rx_lna == bridge_rx_lna
                    and manifest_rx_vga is not None
                    and manifest_rx_vga >= 0
                    and manifest_rx_vga == bridge_rx_vga
                )
                label_matches = (
                    isinstance(manifest_label, str)
                    and bool(manifest_label.strip())
                    and manifest_label != "unqualified"
                    and manifest_label == bridge_label
                )
                tx_confirmed = manifest_tx_enabled is False or (
                    manifest.get("tx_safety_confirmed") is True
                    and bridge.get("tx_safety_confirmed") is True
                )
                elevated_confirmed = (
                    manifest_tx_gain in {None, 0}
                    or manifest_tx_enabled is False
                    or (
                        manifest.get("elevated_tx_gain_confirmed") is True
                        and bridge.get("elevated_tx_gain_confirmed") is True
                    )
                )
                provenance_ok = (
                    manifest_tx_enabled in {True, False}
                    and manifest_tx_enabled == bridge_tx_enabled
                    and gains_match
                    and label_matches
                    and tx_confirmed
                    and elevated_confirmed
                    and directional_amp_match
                    and manifest.get("antenna_power_enabled") is False
                    and bridge.get("antenna_power_enabled") is False
                )
                _gate(
                    gates,
                    failures,
                    "rf_safety_provenance",
                    provenance_ok,
                    (
                        f"path={manifest_label!r}/{bridge_label!r}, "
                        f"tx={manifest_tx_enabled!r}/{bridge_tx_enabled!r}, "
                        f"gains=tx{manifest_tx_gain!r}/{bridge_tx_gain!r} "
                        f"lna{manifest_rx_lna!r}/{bridge_rx_lna!r} "
                        f"vga{manifest_rx_vga!r}/{bridge_rx_vga!r}, "
                        f"amp={manifest.get('rf_amp_enabled')!r}/"
                        f"{bridge.get('rf_amp_enabled')!r}, "
                        f"bias={manifest.get('antenna_power_enabled')!r}/"
                        f"{bridge.get('antenna_power_enabled')!r}"
                    ),
                )

                manifest_leading_ms = manifest.get("tx_leading_ms")
                bridge_leading_ms = bridge.get("tx_leading_ms")
                fixed_baseline_ok = (
                    manifest_tx_enabled is True
                    and bridge_tx_enabled is True
                    and manifest.get("tx_mode") == BASELINE_TX_MODE
                    and bridge.get("tx_mode") == BASELINE_TX_MODE
                    and manifest_tx_gain == BASELINE_TX_GAIN_DB
                    and bridge_tx_gain == BASELINE_TX_GAIN_DB
                    and manifest_rx_lna == BASELINE_RX_LNA_GAIN_DB
                    and bridge_rx_lna == BASELINE_RX_LNA_GAIN_DB
                    and manifest_rx_vga == BASELINE_RX_VGA_GAIN_DB
                    and bridge_rx_vga == BASELINE_RX_VGA_GAIN_DB
                    and manifest_label == BASELINE_RF_PATH_LABEL
                    and bridge_label == BASELINE_RF_PATH_LABEL
                    and manifest_leading_ms == BASELINE_TX_LEADING_MS
                    and bridge_leading_ms == BASELINE_TX_LEADING_MS
                    and manifest.get("gain_control") == "fixed"
                    and bridge.get("gain_control") == "fixed"
                    and manifest.get("rf_amp_enabled") is False
                    and bridge.get("rf_amp_enabled") is False
                    and manifest_rx_amp is False
                    and bridge_rx_amp is False
                    and manifest_tx_amp is False
                    and bridge_tx_amp is False
                    and manifest.get("antenna_power_enabled") is False
                    and bridge.get("antenna_power_enabled") is False
                )
                calibration = _mapping(bridge.get("calibration"))
                selected = _mapping(calibration.get("selected"))
                rx_windows = calibration.get("rx_windows")
                tx_probes = calibration.get("tx_probes")
                selected_rx_proven = isinstance(rx_windows, list) and any(
                    isinstance(window, dict)
                    and window.get("passed") is True
                    and _integer(window.get("lna_gain_db")) == manifest_rx_lna
                    and _integer(window.get("vga_gain_db")) == manifest_rx_vga
                    and window.get("rf_amp_enabled") == manifest_rx_amp
                    for window in rx_windows
                )
                selected_tx_proven = isinstance(tx_probes, list) and any(
                    isinstance(probe, dict)
                    and probe.get("pong") is True
                    and _integer(probe.get("gain_db")) == manifest_tx_gain
                    and probe.get("rf_amp_enabled") == manifest_tx_amp
                    for probe in tx_probes
                )
                rx_normal_search_exhausted = (
                    manifest_rx_amp is False
                    or (
                        isinstance(rx_windows, list)
                        and {
                            (
                                _integer(window.get("lna_gain_db")),
                                _integer(window.get("vga_gain_db")),
                            )
                            for window in rx_windows
                            if isinstance(window, dict)
                            and window.get("rf_amp_enabled") is False
                            and window.get("passed") is False
                        }
                        == set(RX_CANDIDATES)
                    )
                )
                tx_normal_search_exhausted = (
                    manifest_tx_amp is False
                    or (
                        isinstance(tx_probes, list)
                        and {
                            _integer(probe.get("gain_db"))
                            for probe in tx_probes
                            if isinstance(probe, dict)
                            and probe.get("rf_amp_enabled") is False
                            and probe.get("pong") is False
                        }
                        == set(TX_CANDIDATES)
                    )
                )
                automatic_ok = (
                    manifest_tx_enabled is True
                    and bridge_tx_enabled is True
                    and manifest.get("tx_mode") == BASELINE_TX_MODE
                    and bridge.get("tx_mode") == BASELINE_TX_MODE
                    and manifest_label == BASELINE_RF_PATH_LABEL
                    and bridge_label == BASELINE_RF_PATH_LABEL
                    and manifest_leading_ms == BASELINE_TX_LEADING_MS
                    and bridge_leading_ms == BASELINE_TX_LEADING_MS
                    and manifest.get("gain_control") == "automatic"
                    and bridge.get("gain_control") == "automatic"
                    and calibration.get("enabled") is True
                    and calibration.get("state") == "complete"
                    and _integer(selected.get("tx_gain_db")) == manifest_tx_gain
                    and _integer(selected.get("rx_lna_gain_db")) == manifest_rx_lna
                    and _integer(selected.get("rx_vga_gain_db")) == manifest_rx_vga
                    and selected.get("rx_rf_amp_enabled") == manifest_rx_amp
                    and selected.get("tx_rf_amp_enabled") == manifest_tx_amp
                    and manifest_tx_gain is not None
                    and 0 <= manifest_tx_gain <= 47
                    and manifest_rx_lna in range(0, 41, 8)
                    and manifest_rx_vga in range(0, 63, 2)
                    and selected_rx_proven
                    and selected_tx_proven
                    and directional_amp_match
                    and rx_normal_search_exhausted
                    and tx_normal_search_exhausted
                    and manifest.get("antenna_power_enabled") is False
                    and bridge.get("antenna_power_enabled") is False
                )
                _gate(
                    gates,
                    failures,
                    "rf_gain_policy",
                    fixed_baseline_ok or automatic_ok,
                    (
                        f"mode={manifest.get('tx_mode')!r}/{bridge.get('tx_mode')!r}, "
                        f"path={manifest_label!r}/{bridge_label!r}, "
                        f"gains=tx{manifest_tx_gain!r}/{bridge_tx_gain!r} "
                        f"lna{manifest_rx_lna!r}/{bridge_rx_lna!r} "
                        f"vga{manifest_rx_vga!r}/{bridge_rx_vga!r}, "
                        f"lead_ms={manifest_leading_ms!r}/{bridge_leading_ms!r}, "
                        f"gain_control={manifest.get('gain_control')!r}/"
                        f"{bridge.get('gain_control')!r}, "
                        f"calibration={calibration.get('state')!r}"
                    ),
                )

        rx_iq = _mapping(bridge.get("rx_iq"))
        if rx_iq:
            sampled = _integer(rx_iq.get("sampled_complex_samples"))
            clipped = _integer(rx_iq.get("clipped_complex_samples"))
            clipped_fraction = rx_iq.get("clipped_fraction")
            iq_clean = (
                sampled is not None
                and sampled > 0
                and clipped == 0
                and clipped_fraction == 0.0
            )
            _gate(
                gates,
                failures,
                "rx_iq_no_clipping",
                iq_clean,
                (
                    f"samples={sampled!r}, clipped={clipped!r}, "
                    f"fraction={clipped_fraction!r}, "
                    f"peak_abs={rx_iq.get('peak_abs')!r}"
                ),
            )

        for channel in ("0", "1"):
            count = _counter(messages, channel)
            byte_count = _counter(message_bytes, channel)
            _gate(
                gates,
                failures,
                f"channel_{channel}_delivered",
                count is not None and count > 0 and byte_count is not None and byte_count > 0,
                f"messages={count!r}, bytes={byte_count!r}",
            )

        tx_mode = str(bridge.get("tx_mode", ""))
        tx_segments = ack["tx_segments"]
        tx_attempts = ack["tx_attempts"]
        tx_failures = ack["tx_failures"]
        tx_message_count = ack["tx_messages_channel_0"]
        ack_counters = (
            tx_message_count,
            tx_segments,
            tx_attempts,
            ack["ack_received"],
            ack["ack_timeouts"],
            ack["degraded_segments"],
            tx_failures,
        )
        counters_nonnegative = all(
            value is not None and value >= 0 for value in ack_counters
        )
        if tx_mode == "ack":
            # Retries are a successful reliability proof when every completed
            # channel-0 message ultimately received its ACK.  Aggregate bridge
            # counters also include ACK-free channel-1 repair transmissions,
            # so do not require attempts == total segments here.
            ack_ok = (
                counters_nonnegative
                and tx_message_count is not None
                and tx_message_count > 0
                and tx_segments is not None
                and ack["ack_received"] is not None
                and ack["ack_received"] >= tx_message_count
                and ack["degraded_segments"] is not None
                and tx_segments
                == ack["ack_received"] + ack["degraded_segments"]
                and tx_attempts is not None
                and ack["ack_timeouts"] is not None
                and tx_attempts >= tx_segments + ack["ack_timeouts"]
                and tx_failures == 0
            )
            ack_detail = (
                f"mode=ack messages={tx_message_count!r} segments={tx_segments!r} "
                f"attempts={tx_attempts!r} received={ack['ack_received']!r} "
                f"timeouts={ack['ack_timeouts']!r} failures={tx_failures!r}"
            )
        else:
            ack_ok = False
            ack_detail = f"unsupported proof tx_mode={tx_mode or 'missing'}"
        _gate(gates, failures, "command_uplink", ack_ok, ack_detail)

        _gate(
            gates,
            failures,
            "no_host_resource_drops",
            all(
                value == 0
                for value in (
                    drops["hackrf_rx_dropped_blocks"],
                    drops["reassembly_timeouts"],
                    drops["tx_queue_rejections"],
                )
            ),
            ", ".join(f"{key}={value!r}" for key, value in drops.items()),
        )
        if drops["reassembly_drops"] not in {None, 0}:
            warnings.append(
                "one or more RF messages were not reassembled; isolated telemetry loss "
                "is expected while a single half-duplex HackRF is transmitting"
            )
        _gate(
            gates,
            failures,
            "no_backpressure",
            not backpressure_detected,
            (
                f"detected={backpressure_detected}, pty_backlog={pty_backlogs}, "
                f"tx_queue_rejections={drops['tx_queue_rejections']!r}, "
                f"bridge_log_matches={len(backpressure_lines)}"
            ),
        )
        _gate(
            gates,
            failures,
            "bridge_runtime_clean",
            last_error in {None, ""} and _counter(bridge, "reconnects") == 0,
            f"last_error={last_error!r}, reconnects={bridge.get('reconnects')!r}",
        )

    event_summary: dict[str, Any] = {
        "records": len(event_records),
        "command_events": {},
        "gates": {},
        "failure_events": [],
    }
    if event_records:
        dispatched = _event_matches(event_records, "cmdDisp.OpCodeDispatched")
        completed = _event_matches(event_records, "cmdDisp.OpCodeCompleted")
        event_summary["command_events"] = {
            "opcode_dispatched": len(dispatched),
            "opcode_completed": len(completed),
        }
        _gate(
            gates,
            failures,
            "gds_command_completion",
            len(dispatched) > 0 and len(completed) >= len(dispatched),
            f"dispatched={len(dispatched)}, completed={len(completed)}",
        )
        if len(dispatched) != len(completed):
            warnings.append(
                "GDS command dispatch/completion event counts differ; validate command "
                "effects because half-duplex TX can hide one telemetry event"
            )

        for key, suffix, required, predicate in EVENT_GATES:
            matches = _event_matches(event_records, suffix, predicate)
            passed = bool(matches)
            event_summary["gates"][key] = {
                "required": required,
                "passed": passed,
                "count": len(matches),
                "latest": _event_reference(matches[-1]) if matches else None,
            }
            _gate(
                gates,
                failures,
                f"event_{key}",
                passed,
                f"event={suffix}, matches={len(matches)}",
                required=required,
            )

        failure_events = [
            record
            for record in event_records
            if any(
                _is_event(str(record.get("name", "")), suffix)
                for suffix in FAILURE_EVENT_SUFFIXES
            )
        ]
        event_summary["failure_events"] = [
            _event_reference(record) for record in failure_events
        ]
        _gate(
            gates,
            failures,
            "no_c3m_failure_events",
            not failure_events,
            f"matches={len(failure_events)}",
        )

    payload_summary: dict[str, Any] = {}
    if payload is not None:
        resolved_payload = _resolve_payload_file(payload_run, payload, payload_file)
        payload_source: dict[str, Any] = {
            "path": str(resolved_payload) if resolved_payload else None,
            "present": resolved_payload is not None,
        }
        sources["payload_file"] = payload_source
        local_sha: str | None = None
        local_size: int | None = None
        if resolved_payload is None:
            payload_source.update({"valid": False, "error": "not found"})
            _add_failure(failures, "missing required artifact: payload_file")
        else:
            try:
                local_size = resolved_payload.stat().st_size
                local_sha = _sha256_file(resolved_payload)
                payload_source.update(
                    {"valid": True, "size_bytes": local_size, "sha256": local_sha}
                )
            except OSError as exc:
                payload_source.update({"valid": False, "error": str(exc)})
                _add_failure(failures, "invalid required artifact: payload_file")

        result = str(payload.get("result") or payload.get("status") or "")
        failed_state = _payload_failed_state(payload)
        failure_reason = payload.get("failure_reason")
        decode = payload.get("decode")
        elapsed = _number(payload.get("elapsed_seconds"))
        timing_band = _timing_band(elapsed)
        total_bytes = _integer(payload.get("total_bytes"))
        received_bytes = _integer(payload.get("received_bytes"))
        total_packets = _integer(payload.get("total_packets"))
        received_packets = _integer(payload.get("received_packets"))
        expected_crc = _integer(payload.get("expected_crc"))
        actual_crc = _integer(payload.get("actual_crc"))
        recorded_sha = payload.get("sha256")

        pi_evidence: dict[str, Any] = {
            "path": pi_path,
            "sha256": pi_sha256,
            "matches_local": None,
        }
        if pi_sha256 is not None and local_sha is not None:
            pi_evidence["matches_local"] = pi_sha256 == local_sha
        if pi_path and pi_sha256 is None:
            warnings.append("Pi path was recorded without a Pi SHA-256 to compare")

        payload_summary = {
            "run_id": payload.get("run_id"),
            "result": result,
            "failure_reason": failure_reason,
            "product_id": payload.get("product_id"),
            "transfer_id": payload.get("transfer_id"),
            "crc_ok": payload.get("crc_ok"),
            "expected_crc": expected_crc,
            "actual_crc": actual_crc,
            "partial": payload.get("partial"),
            "completion_reason": payload.get("completion_reason"),
            "timeout_reason": payload.get("timeout_reason"),
            "total_bytes": total_bytes,
            "received_bytes": received_bytes,
            "total_packets": total_packets,
            "received_packets": received_packets,
            "missing_packets": payload.get("missing_packets"),
            "retry_rounds": payload.get("retry_rounds"),
            "elapsed_seconds": elapsed,
            "timing_band": timing_band,
            "timing_targets_seconds": {
                "nominal": NOMINAL_TRANSFER_TARGET_S,
                "cutoff": LIVE_DEMO_CUTOFF_S,
            },
            "decode": decode,
            "file": {
                "path": str(resolved_payload) if resolved_payload else None,
                "size_bytes": local_size,
                "sha256": local_sha,
                "recorded_sha256": recorded_sha,
            },
            "pi_evidence": pi_evidence,
        }

        _gate(
            gates,
            failures,
            "payload_result_complete",
            result.casefold() == "complete" and failed_state is None,
            f"result={result or 'missing'}, failed_state={failed_state!r}",
        )
        _gate(
            gates,
            failures,
            "payload_no_failure_reason",
            failure_reason in {None, ""},
            f"failure_reason={failure_reason!r}",
        )
        _gate(
            gates,
            failures,
            "payload_crc",
            payload.get("crc_ok") is True
            and expected_crc is not None
            and expected_crc == actual_crc,
            (
                f"crc_ok={payload.get('crc_ok')!r}, expected={expected_crc!r}, "
                f"actual={actual_crc!r}"
            ),
        )
        _gate(
            gates,
            failures,
            "payload_decode",
            isinstance(decode, dict) and bool(decode) and failed_state is None,
            f"decode_present={isinstance(decode, dict) and bool(decode)}, failed_state={failed_state!r}",
        )
        _gate(
            gates,
            failures,
            "payload_complete_counts",
            (
                payload.get("partial") is False
                and _integer(payload.get("missing_packets")) == 0
                and total_bytes is not None
                and total_bytes > 0
                and received_bytes == total_bytes
                and total_packets is not None
                and total_packets > 0
                and received_packets == total_packets
            ),
            (
                f"partial={payload.get('partial')!r}, missing={payload.get('missing_packets')!r}, "
                f"bytes={received_bytes!r}/{total_bytes!r}, "
                f"packets={received_packets!r}/{total_packets!r}"
            ),
        )
        _gate(
            gates,
            failures,
            "payload_file_size",
            local_size is not None and local_size > 0 and local_size == total_bytes,
            f"local={local_size!r}, declared={total_bytes!r}",
        )
        _gate(
            gates,
            failures,
            "payload_local_sha",
            isinstance(recorded_sha, str)
            and local_sha is not None
            and recorded_sha.casefold() == local_sha,
            f"recorded={recorded_sha!r}, computed={local_sha!r}",
        )
        _gate(
            gates,
            failures,
            "payload_timing",
            elapsed is not None and elapsed > 0 and elapsed <= LIVE_DEMO_CUTOFF_S,
            f"elapsed_seconds={elapsed!r}, band={timing_band!r}, cutoff={LIVE_DEMO_CUTOFF_S}",
        )
        if timing_band == "degraded":
            warnings.append(
                f"payload completed after the {NOMINAL_TRANSFER_TARGET_S:.0f}s nominal target"
            )

        if pi_sha256 is not None:
            _gate(
                gates,
                failures,
                "payload_pi_sha",
                local_sha is not None and pi_sha256 == local_sha,
                f"pi={pi_sha256!r}, local={local_sha!r}, pi_path={pi_path!r}",
            )

        consistency = _event_payload_consistency(event_records, payload)
        payload_summary["event_consistency"] = consistency
        for key, check in consistency.items():
            _gate(
                gates,
                failures,
                f"payload_event_{key}",
                bool(check["passed"]),
                f"expected={check['expected']!r}, observed={check['observed']!r}",
            )

        ordered_chain = _ordered_demo_chain(event_records, payload)
        payload_summary["ordered_demo_chain"] = ordered_chain
        _gate(
            gates,
            failures,
            "ordered_demo_flow",
            bool(ordered_chain["passed"]),
            str(ordered_chain["detail"]),
        )

    overall_status = "pass" if not failures else "fail"
    return {
        "schema": SCHEMA_VERSION,
        "generated_at": _utc_now(),
        "supervisor_run": str(supervisor_run),
        "overall": {
            "status": overall_status,
            "passed": overall_status == "pass",
            "failures": failures,
            "warnings": warnings,
        },
        "sources": sources,
        "gates": gates,
        "supervisor": supervisor_summary,
        "bridge": bridge_summary,
        "events": event_summary,
        "payload": payload_summary,
    }


def _markdown_cell(value: object) -> str:
    if value is None:
        return "—"
    if isinstance(value, bool):
        return "yes" if value else "no"
    return str(value).replace("|", "\\|").replace("\n", " ")


def render_markdown(summary: dict[str, Any]) -> str:
    overall = _mapping(summary.get("overall"))
    supervisor = _mapping(summary.get("supervisor"))
    bridge = _mapping(summary.get("bridge"))
    payload = _mapping(summary.get("payload"))
    ack = _mapping(bridge.get("command_ack"))
    channels = _mapping(bridge.get("channels"))
    drops = _mapping(bridge.get("drops"))
    event_gates = _mapping(_mapping(summary.get("events")).get("gates"))
    command_events = _mapping(_mapping(summary.get("events")).get("command_events"))
    payload_file = _mapping(payload.get("file"))
    pi_evidence = _mapping(payload.get("pi_evidence"))
    decode = _mapping(payload.get("decode"))
    ordered_chain = _mapping(payload.get("ordered_demo_chain"))
    rx_iq = _mapping(bridge.get("rx_iq"))

    status = "PASS" if overall.get("passed") is True else "FAIL"
    lines = [
        "# HackRF HIL Proof Summary",
        "",
        f"**Overall: {status}**",
        "",
        f"- Supervisor run: `{summary.get('supervisor_run')}`",
        f"- Generated: `{summary.get('generated_at')}`",
        "",
        "## RF and command path",
        "",
        "| Evidence | Value |",
        "|---|---:|",
        f"| RF path | {_markdown_cell(supervisor.get('rf_path_label'))} |",
        f"| TX / RX LNA / RX VGA gain (dB) | {_markdown_cell(bridge.get('tx_gain'))} / {_markdown_cell(bridge.get('rx_lna_gain'))} / {_markdown_cell(bridge.get('rx_vga_gain'))} |",
        f"| RF amp / antenna bias power | {_markdown_cell(bridge.get('rf_amp_enabled'))} / {_markdown_cell(bridge.get('antenna_power_enabled'))} |",
        f"| IQ clipped / sampled | {_markdown_cell(rx_iq.get('clipped_complex_samples'))} / {_markdown_cell(rx_iq.get('sampled_complex_samples'))} |",
        f"| IQ peak abs / RMS dBFS | {_markdown_cell(rx_iq.get('peak_abs'))} / {_markdown_cell(rx_iq.get('last_block_complex_rms_dbfs'))} |",
        f"| TX mode | {_markdown_cell(ack.get('tx_mode'))} |",
        f"| Channel-0 TX messages | {_markdown_cell(ack.get('tx_messages_channel_0'))} |",
        f"| TX segments / attempts | {_markdown_cell(ack.get('tx_segments'))} / {_markdown_cell(ack.get('tx_attempts'))} |",
        f"| ACK received / timeouts | {_markdown_cell(ack.get('ack_received'))} / {_markdown_cell(ack.get('ack_timeouts'))} |",
        f"| TX failures | {_markdown_cell(ack.get('tx_failures'))} |",
        f"| GDS dispatched / completed | {_markdown_cell(command_events.get('opcode_dispatched'))} / {_markdown_cell(command_events.get('opcode_completed'))} |",
        f"| Channel 0 messages / bytes | {_markdown_cell(_mapping(channels.get('0')).get('messages'))} / {_markdown_cell(_mapping(channels.get('0')).get('message_bytes'))} |",
        f"| Channel 1 messages / bytes | {_markdown_cell(_mapping(channels.get('1')).get('messages'))} / {_markdown_cell(_mapping(channels.get('1')).get('message_bytes'))} |",
        f"| HackRF drops | {_markdown_cell(drops.get('hackrf_rx_dropped_blocks'))} |",
        f"| Reassembly drops / timeouts / duplicates | {_markdown_cell(drops.get('reassembly_drops'))} / {_markdown_cell(drops.get('reassembly_timeouts'))} / {_markdown_cell(drops.get('reassembly_duplicates'))} |",
        f"| TX queue rejections | {_markdown_cell(drops.get('tx_queue_rejections'))} |",
        "",
        "## C3M event gates",
        "",
        "| Gate | Required | Result | Count |",
        "|---|:---:|:---:|---:|",
    ]
    for key, value in event_gates.items():
        gate = _mapping(value)
        lines.append(
            f"| {_markdown_cell(key)} | {'yes' if gate.get('required') else 'no'} | "
            f"{'PASS' if gate.get('passed') else 'MISS'} | {_markdown_cell(gate.get('count'))} |"
        )

    lines.extend(
        [
            "",
            "## Payload",
            "",
            "| Evidence | Value |",
            "|---|---|",
            f"| Run / result | {_markdown_cell(payload.get('run_id'))} / {_markdown_cell(payload.get('result'))} |",
            f"| Product / transfer | {_markdown_cell(payload.get('product_id'))} / {_markdown_cell(payload.get('transfer_id'))} |",
            f"| CRC | {_markdown_cell(payload.get('crc_ok'))} (`{_markdown_cell(payload.get('actual_crc'))}`) |",
            f"| Bytes | {_markdown_cell(payload.get('received_bytes'))} / {_markdown_cell(payload.get('total_bytes'))} |",
            f"| Packets | {_markdown_cell(payload.get('received_packets'))} / {_markdown_cell(payload.get('total_packets'))} |",
            f"| Timing | {_markdown_cell(payload.get('elapsed_seconds'))} s ({_markdown_cell(payload.get('timing_band'))}) |",
            f"| Decode | {_markdown_cell(decode.get('width'))}x{_markdown_cell(decode.get('height'))} / {_markdown_cell(decode.get('pixels'))} pixels |",
            f"| Ordered demo flow | {'PASS' if ordered_chain.get('passed') else 'FAIL'} — {_markdown_cell(ordered_chain.get('detail'))} |",
            f"| Local file | `{_markdown_cell(payload_file.get('path'))}` |",
            f"| Local size | {_markdown_cell(payload_file.get('size_bytes'))} bytes |",
            f"| Local SHA-256 | `{_markdown_cell(payload_file.get('sha256'))}` |",
        ]
    )
    if pi_evidence.get("sha256") is not None or pi_evidence.get("path") is not None:
        lines.extend(
            [
                f"| Pi path | `{_markdown_cell(pi_evidence.get('path'))}` |",
                f"| Pi SHA-256 | `{_markdown_cell(pi_evidence.get('sha256'))}` |",
                f"| Pi/local hash match | {_markdown_cell(pi_evidence.get('matches_local'))} |",
            ]
        )

    failures = overall.get("failures") if isinstance(overall.get("failures"), list) else []
    warnings = overall.get("warnings") if isinstance(overall.get("warnings"), list) else []
    if failures:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in failures)
    if warnings:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in warnings)
    lines.append("")
    return "\n".join(lines)


def write_proof(supervisor_run: Path, summary: dict[str, Any]) -> tuple[Path, Path]:
    json_path = supervisor_run / "proof-summary.json"
    markdown_path = supervisor_run / "proof-summary.md"
    _atomic_write_text(
        json_path, json.dumps(summary, indent=2, sort_keys=True) + "\n"
    )
    _atomic_write_text(markdown_path, render_markdown(summary))
    return json_path, markdown_path


def _sha256_argument(value: str) -> str:
    normalized = value.strip().casefold()
    if re.fullmatch(r"[0-9a-f]{64}", normalized) is None:
        raise argparse.ArgumentTypeError("SHA-256 must be exactly 64 hexadecimal characters")
    return normalized


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Collect HackRF/GDS/payload HIL evidence without touching hardware or SSH."
        )
    )
    parser.add_argument("supervisor_run", type=Path, help="Supervisor run directory")
    parser.add_argument(
        "--payload-run", type=Path, required=True, help="Payload receiver run.json"
    )
    parser.add_argument(
        "--event-log",
        type=Path,
        help="GDS event.log override (default: SUPERVISOR_RUN/gds/event.log)",
    )
    parser.add_argument(
        "--payload-file",
        type=Path,
        help="Local payload file override when run.json outputs cannot resolve it",
    )
    parser.add_argument(
        "--pi-sha256",
        type=_sha256_argument,
        help="Caller-obtained Pi SHA-256; the collector never opens SSH",
    )
    parser.add_argument(
        "--pi-path", help="Caller-obtained Pi payload path to record in the proof"
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    run_dir = args.supervisor_run.expanduser().resolve()
    if not run_dir.is_dir():
        print(f"ERROR supervisor run directory does not exist: {run_dir}", file=sys.stderr)
        return 2

    summary = collect_proof(
        run_dir,
        args.payload_run.expanduser(),
        event_log=args.event_log.expanduser() if args.event_log else None,
        payload_file=args.payload_file.expanduser() if args.payload_file else None,
        pi_sha256=args.pi_sha256,
        pi_path=args.pi_path,
    )
    json_path, markdown_path = write_proof(run_dir, summary)
    status = str(_mapping(summary.get("overall")).get("status", "fail")).upper()
    print(f"HIL_PROOF_{status} json={json_path} markdown={markdown_path}")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
