#!/usr/bin/env python3
"""Accept verified raw capture/downlink and restart-safe storage through Yamcs.

Every command, event, parameter and downloaded ground object used as acceptance
evidence is obtained through the Yamcs HTTP API. The runner never reads the
simulator's source product file.
"""
import argparse
import binascii
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import secrets
import time
from urllib import error, parse, request

from demo import fresh_parameter, numeric, returned_event
from passthrough import mode_value, parameter, wait_for_running
from smoke import INSTANCE, PROCESSOR, ROOT, URL, api, events, wait_for_event

DEPLOYMENT = "ArtemisRpiTeensyDeployment"
MANAGER = f"{DEPLOYMENT}.payloadManager"
SCIENCE = f"{DEPLOYMENT}.scienceApp"
COMMS = f"{DEPLOYMENT}.commsApp"
STORAGE = f"{DEPLOYMENT}.storageManager"
BUCKET = "fprimeFilesIn"
POLL_SECONDS = 0.25
DEFAULT_TIMEOUT = 45
MAX_STORAGE_PRODUCTS = 16


def stamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def current_run():
    current = json.loads((ROOT / "yamcs" / "run" / "current.json").read_text())
    required = ("run_dir", "local_run_id", "started_at", "storage_root")
    missing = [key for key in required if not current.get(key)]
    if missing:
        raise RuntimeError(f"Yamcs stack metadata is missing {', '.join(missing)}")
    current["run_dir"] = str(Path(current["run_dir"]).resolve())
    current["storage_root"] = str(Path(current["storage_root"]).resolve())
    current["started_at"] = stamp(current["started_at"]).isoformat()
    return current


def issue(name, args=None):
    """Issue a Yamcs command with a per-run sequence number."""
    short_name = name.split(".")[-1]
    listing = api(f"/api/mdb/{INSTANCE}/commands?" + parse.urlencode({"q": short_name, "limit": 500}))
    suffix = "/" + name.replace(".", "/")
    matches = [entry["qualifiedName"] for entry in listing.get("commands", [])
               if entry["qualifiedName"].endswith(suffix)]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one Yamcs command for {name}, found {matches}")
    body = {"args": args or {}, "origin": f"n2-completion-storage-{os.getpid()}",
            "sequenceNumber": secrets.randbelow(2_147_483_646) + 1}
    return api(f"/api/processors/{INSTANCE}/{PROCESSOR}/commands/" +
               parse.quote(matches[0], safe="/"), body)


def returned(command, component, event_name, predicate=None, timeout=DEFAULT_TIMEOUT):
    """Wait for a returned event belonging to a specific released command."""
    released = stamp(command["generationTime"])
    expected_type = f"{DEPLOYMENT}.{component}.{event_name}"
    return wait_for_event(event_name, timeout=timeout, after=released,
                          predicate=lambda event: event.get("type") == expected_type
                          and (predicate is None or predicate(event)))


def extra(event, name):
    return str(event.get("extra", {}).get(name))


def download_object(name):
    if not name or "/" in name or "\\" in name:
        raise RuntimeError(f"Unsafe Yamcs object name {name!r}")
    endpoint = (f"/api/storage/buckets/{parse.quote(BUCKET, safe='')}/objects/"
                f"{parse.quote(name, safe='')}?instance={parse.quote(INSTANCE, safe='')}")
    with request.urlopen(URL + endpoint, timeout=15) as response:
        return response.read()


def history_entry(command):
    """Read one exact command-history record by the Yamcs-issued ID."""
    command_id = command.get("id")
    if not command_id:
        raise RuntimeError(f"Yamcs issue response omitted command ID: {command}")
    entry = api(f"/api/archive/{INSTANCE}/commands/" + parse.quote(command_id, safe=""))
    actual_id = entry.get("commandId", {})
    actual_origin = entry.get("origin", actual_id.get("origin"))
    actual_sequence = entry.get("sequenceNumber", actual_id.get("sequenceNumber"))
    if (entry.get("id") != command_id or actual_origin != command.get("origin")
            or int(actual_sequence) != int(command.get("sequenceNumber"))):
        raise RuntimeError(f"Command-history identity mismatch for {command_id}: {entry}")
    return entry


def yamcs_value(value):
    """Unwrap Yamcs Value JSON while retaining enum string representations."""
    if not isinstance(value, dict):
        return value
    value_type = value.get("type")
    if value_type == "ENUMERATED":
        return value.get("stringValue", value.get("sint64Value"))
    for key in ("stringValue", "booleanValue", "uint32Value", "sint32Value", "uint64Value",
                "sint64Value", "doubleValue", "floatValue", "timestampValue"):
        if key in value:
            return value[key]
    return value


def history_attributes(entry):
    """Return the newest observed value for each exact Yamcs history attribute."""
    out = {}
    for item in entry.get("attr") or entry.get("attributes", []):
        name = item.get("name")
        if not name:
            continue
        old = out.get(name)
        if old is None or int(item.get("time", "0")) >= int(old.get("time", "0")):
            out[name] = item
    return out


def wait_history(command, expected, timeout=DEFAULT_TIMEOUT):
    """Poll exact command history until all expected status attributes appear."""
    deadline = time.monotonic() + timeout
    latest = None
    while time.monotonic() < deadline:
        try:
            latest = history_entry(command)
            attributes = history_attributes(latest)
            matched = {name: yamcs_value(attributes[name].get("value"))
                       for name in expected if name in attributes}
            if all(name in matched and str(matched[name]) == str(value)
                   for name, value in expected.items()):
                return latest
        except error.HTTPError as exc:
            if exc.code != 404:
                raise
        time.sleep(POLL_SECONDS)
    raise RuntimeError(f"Command history {command.get('id')} did not reach {expected}; latest={latest}")


def wait_history_any(command, expected, timeout=DEFAULT_TIMEOUT):
    """Poll command history until each attribute matches one accepted value."""
    deadline = time.monotonic() + timeout
    latest = None
    while time.monotonic() < deadline:
        try:
            latest = history_entry(command)
            attributes = history_attributes(latest)
            values = {name: yamcs_value(attributes[name].get("value"))
                      for name in expected if name in attributes}
            if all(name in values and str(values[name]).upper() in
                   {str(value).upper() for value in allowed}
                   for name, allowed in expected.items()):
                return latest
        except error.HTTPError as exc:
            if exc.code != 404:
                raise
        time.sleep(POLL_SECONDS)
    raise RuntimeError(f"Command history {command.get('id')} did not reach {expected}; latest={latest}")


def require_history_values(entry, expected):
    attrs = history_attributes(entry)
    actual = {name: yamcs_value(attrs[name].get("value"))
              for name in expected if name in attrs}
    failures = {name: {"expected": sorted(values), "actual": actual.get(name)}
                for name, values in expected.items()
                if name not in actual or str(actual[name]).upper() not in
                {str(value).upper() for value in values}}
    if failures:
        raise RuntimeError(f"Command history stage mismatch: {failures}")
    return actual


def snapshot_history(entry):
    return {name: {"value": yamcs_value(item.get("value")), "time": item.get("time"),
                   "message": item.get("message")}
            for name, item in history_attributes(entry).items()}


def assert_no_command_success(command):
    """Reject a stale or premature overall success on this exact command."""
    entry = history_entry(command)
    attrs = history_attributes(entry)
    item = attrs.get("CommandComplete_Status")
    if item and str(yamcs_value(item.get("value"))).upper() == "OK":
        raise RuntimeError(f"Unexpected CommandComplete=OK on command {command['id']}")
    return entry


def assert_no_success(command, timeout=2):
    """Reject a stale or premature successful overall-completion status."""
    deadline = time.monotonic() + timeout
    observed = []
    while time.monotonic() < deadline:
        try:
            entry = history_entry(command)
            attrs = history_attributes(entry)
            item = attrs.get("CommandComplete_Status")
            if item:
                value = yamcs_value(item.get("value"))
                observed.append({"name": "CommandComplete_Status", "value": value})
                if str(value).upper() == "OK":
                    raise RuntimeError(f"Unexpected CommandComplete=OK on command {command['id']}")
        except error.HTTPError as exc:
            if exc.code != 404:
                raise
        time.sleep(POLL_SECONDS)
    return observed


def verify_raw_receipt(event, expected_run_id, expected_product_id=None,
                       expected_transfer_id=None, expected_digest=None,
                       expected_crc=None, expected_size=None):
    """Download and verify Yamcs receipt and opaque raw object bytes."""
    receipt_name = event.get("extra", {}).get("receiptObject")
    if not isinstance(receipt_name, str):
        raise RuntimeError(f"Ground receipt event has no receipt object: {event}")
    receipt_bytes = download_object(receipt_name)
    receipt = json.loads(receipt_bytes)
    object_name = receipt.get("objectName")
    raw = download_object(object_name)
    digest = hashlib.sha256(raw).hexdigest()
    crc = binascii.crc_hqx(raw, 0xFFFF)
    product_id = int(receipt["productId"])
    transfer_id = int(receipt["transferId"])
    reported_crc = int(receipt["crc16Ccitt"])
    checks = {
        "verified_ground_receipt": receipt.get("verifiedGroundReceipt") is True,
        "run_id": receipt.get("runId") == expected_run_id,
        "product_id": expected_product_id is None or product_id == expected_product_id,
        "transfer_id": expected_transfer_id is None or transfer_id == expected_transfer_id,
        "receipt_bytes": int(receipt.get("bytes", -1)) == len(raw),
        "receipt_sha256": receipt.get("sha256") == digest,
        "receipt_crc16_ccitt": reported_crc == crc,
        "receipt_object_identity": receipt.get("receiptObject") == receipt_name,
        "receipt_raw_object_identity": receipt.get("objectName") == object_name,
        "event_bytes": extra(event, "bytes") == str(len(raw)),
        "event_sha256": extra(event, "sha256") == digest,
        "event_crc16_ccitt": extra(event, "crc16Ccitt") == str(crc),
        "expected_digest": expected_digest is None or digest == expected_digest,
        "expected_crc": expected_crc is None or crc == expected_crc,
        "expected_size": expected_size is None or len(raw) == expected_size,
        "event_identity": extra(event, "productId") == str(product_id)
        and extra(event, "transferId") == str(transfer_id)
        and extra(event, "runId") == expected_run_id,
    }
    if not all(checks.values()):
        raise RuntimeError(f"Ground receipt/raw verification failed: {checks}; receipt={receipt}")
    return {"receipt_object": receipt_name, "receipt": receipt,
            "raw_object": object_name, "bytes": len(raw), "sha256": digest,
            "crc16_ccitt": crc, "raw": raw, "checks": checks}


def event_matches(name, after, expected_type, predicate=lambda event: True):
    return [event for event in events(name)
            if event.get("type") == expected_type
            and stamp(event["receptionTime"]) >= after
            and predicate(event)]


def wait_named_event(name, after, expected_type, predicate=lambda event: True,
                     timeout=DEFAULT_TIMEOUT):
    return wait_for_event(name, timeout=timeout, after=after,
                          predicate=lambda event: event.get("type") == expected_type
                          and predicate(event))


def storage_count():
    return numeric(parameter(f"{STORAGE}.StoredProducts"))


def storage_ready(after=None):
    if after is None:
        value = parameter(f"{STORAGE}.StorageReady")
    else:
        value = fresh_parameter(f"{STORAGE}.StorageReady", after)
    eng = value.get("engValue", {})
    for key in ("boolValue", "booleanValue"):
        if key in eng and isinstance(eng[key], bool):
            return eng[key], value
    # Yamcs exposes F' boolean channels as an ENUMERATED engineering value
    # with stringValue "True"/"False" and raw UINT32 255/0. Do not treat an
    # arbitrary nonempty enum label (or raw integer 255) as truthy.
    if eng.get("type") == "ENUMERATED" and eng.get("stringValue") in {"True", "False"}:
        return eng["stringValue"] == "True", value
    raise RuntimeError(f"StorageReady is not a Yamcs boolean value: {value}")


def configure_simulation(mode="NORMAL", delay_ms=0, timeout_ms=8000):
    command = issue(f"{MANAGER}.CONFIGURE_SIMULATION", {
        "faultMode": mode, "delayMs": delay_ms, "timeoutMs": timeout_ms,
    })
    event = returned(command, "payloadManager", "SimulationConfigured",
                     lambda item: extra(item, "faultMode") == mode
                     and extra(item, "delayMs") == str(delay_ms)
                     and extra(item, "timeoutMs") == str(timeout_ms))
    return {"command": command, "event": event}


def capture_status(command, token, states, timeout=DEFAULT_TIMEOUT):
    allowed = {states} if isinstance(states, str) else set(states)
    return returned(command, "scienceApp", "VerifiedCaptureStatus",
                     lambda event: extra(event, "verificationToken") == str(token)
                     and extra(event, "state") in allowed, timeout=timeout)


def downlink_status(command, token, states, timeout=DEFAULT_TIMEOUT):
    state_numbers = {"DOWNLINKING": "1", "DONE": "2", "ABORTED": "3", "ERROR": "4"}
    requested = {states} if isinstance(states, str) else set(states)
    allowed = {state_numbers.get(str(state), str(state)) for state in requested}
    return returned(command, "commsApp", "VerifiedDownlinkStatus",
                     lambda event: extra(event, "verificationToken") == str(token)
                     and extra(event, "stateValue") in allowed, timeout=timeout)


def save_ground_bytes(current, product_id, raw, digest):
    destination = Path(current["run_dir"]) / f"yamcs-product-{product_id}-{digest[:12]}.bin"
    destination.write_bytes(raw)
    return str(destination)


def status_history(command, expected, timeout=DEFAULT_TIMEOUT):
    entry = wait_history_any(command, expected, timeout=timeout)
    require_history_values(entry, expected)
    return {"entry": entry, "stages": snapshot_history(entry)}


def assert_no_downstream_events(command, timeout=1):
    """Prove a rejected capture did not publish storage or delivery events."""
    after = stamp(command["generationTime"])
    forbidden_names = ("ScienceStored", "ScienceProductCommitted", "PayloadDownlinkStarted",
                       "PayloadDownlinkComplete", "GroundScienceReceiptVerified")
    deadline = time.monotonic() + timeout
    last = []
    while time.monotonic() < deadline:
        last = []
        for name in forbidden_names:
            if name == "GroundScienceReceiptVerified":
                last.extend(event for event in events(name)
                            if stamp(event["receptionTime"]) >= after)
            else:
                last.extend(event_matches(name, after, f"{DEPLOYMENT}.storageManager.{name}")
                            if name.startswith("Science") else
                            event_matches(name, after, f"{DEPLOYMENT}.payloadDownlinkApp.{name}"))
        if last:
            raise RuntimeError(f"Rejected operation published downstream success: {last}")
        time.sleep(POLL_SECONDS)
    return last


def reject_capture(current, token, duration, state, timeout=DEFAULT_TIMEOUT):
    before_count = storage_count()
    command = issue(f"{SCIENCE}.VERIFIED_COLLECT_AND_DOWNLINK", {
        "verificationToken": token, "durationMinutes": duration,
    })
    accepted = None
    if state in {"FAILED", "TIMED_OUT"}:
        accepted = capture_status(command, token, "ACCEPTED", timeout=timeout)
    rejected = capture_status(command, token, state, timeout=timeout)
    if state in {"INVALID", "BUSY"}:
        expected = {"PayloadAccepted_Status": {"NOK"}, "PayloadTerminal_Status": {"NOK"},
                    "CommandComplete_Status": {"NOK"}}
    elif state == "FAILED":
        expected = {"PayloadAccepted_Status": {"OK"}, "PayloadTerminal_Status": {"NOK"},
                    "CommandComplete_Status": {"NOK"}}
    elif state == "TIMED_OUT":
        expected = {"PayloadAccepted_Status": {"OK"}, "PayloadTerminal_Status": {"TIMEOUT"},
                    "CommandComplete_Status": {"NOK"}}
    else:
        raise ValueError(f"Unsupported rejected capture state {state}")
    history = status_history(command, expected, timeout=timeout)
    assert_no_command_success(command)
    own_downlink = event_matches(
        "VerifiedDownlinkStatus", stamp(command["generationTime"]),
        f"{COMMS}.VerifiedDownlinkStatus",
        lambda event: extra(event, "verificationToken") == str(token))
    if own_downlink:
        raise RuntimeError(f"Rejected {state} token {token} started a verified downlink: {own_downlink}")
    if state != "BUSY":
        assert_no_downstream_events(command)
        if storage_count() != before_count:
            raise RuntimeError(f"Rejected {state} capture changed StoredProducts: {before_count} -> {storage_count()}")
    return {"command": command, "accepted": accepted, "status_event": rejected, "history": history,
            "stored_products_before": before_count, "stored_products_after": storage_count(),
            "result": "PASS"}


def wait_capture_delivery(current, command, token, accepted=None,
                          timeout=DEFAULT_TIMEOUT, label="capture"):
    released = stamp(command["generationTime"])
    accepted = accepted or capture_status(command, token, "ACCEPTED", timeout=timeout)
    request_id = int(accepted["extra"]["requestId"])
    generation = int(accepted["extra"]["generation"])
    if request_id == 0 or generation == 0:
        raise RuntimeError(f"Verified capture admission omitted a nonzero owner identity: {accepted}")
    terminal = capture_status(command, token, "COMPLETED", timeout=timeout)
    if (int(terminal["extra"].get("requestId", -1)) != request_id
            or int(terminal["extra"].get("generation", -1)) != generation):
        raise RuntimeError(f"Capture terminal does not belong to its accepted request/generation: {terminal}")
    product_id = int(terminal["extra"]["productId"])
    byte_count = int(terminal["extra"]["productBytes"])
    source_crc = int(terminal["extra"]["sourceCrc"])
    committed = returned(command, "storageManager", "ScienceProductCommitted",
                         lambda event: int(event["extra"].get("productId", -1)) == product_id
                         and int(event["extra"].get("productBytes", -1)) == byte_count
                         and int(event["extra"].get("sourceCrc", -1)) == source_crc,
                         timeout=timeout)
    stored = returned(command, "storageManager", "ScienceStored",
                       lambda event: int(event["extra"].get("productBytes", -1)) == byte_count,
                       timeout=timeout)
    started = returned(command, "payloadDownlinkApp", "PayloadDownlinkStarted",
                       lambda event: int(event["extra"].get("productId", -1)) == product_id,
                       timeout=timeout)
    transfer_id = int(started["extra"]["transferId"])
    if int(started["extra"].get("byteCount", -1)) != byte_count:
        raise RuntimeError(f"Sender start size disagrees with committed product: {started}")
    sender_complete = wait_named_event(
        "PayloadDownlinkComplete", released, f"{DEPLOYMENT}.payloadDownlinkApp.PayloadDownlinkComplete",
        lambda event: int(event.get("extra", {}).get("transferId", -1)) == transfer_id,
        timeout=timeout)
    # Automatic capture downlinks intentionally use token zero in CommsApp;
    # their authoritative sender evidence is the transfer-scoped event and the
    # command verifier's SenderComplete history stage.
    verified_downlink = None
    receipt_event = wait_named_event(
        "GroundScienceReceiptVerified", released, "GroundScienceReceiptVerified",
        lambda event: extra(event, "runId") == current["local_run_id"]
        and extra(event, "productId") == str(product_id)
        and extra(event, "transferId") == str(transfer_id), timeout=timeout)
    receipt = verify_raw_receipt(
        receipt_event, current["local_run_id"], expected_product_id=product_id,
        expected_transfer_id=transfer_id, expected_crc=source_crc, expected_size=byte_count)
    raw_path = save_ground_bytes(current, product_id, receipt.pop("raw"), receipt["sha256"])
    history = status_history(command, {
        "PayloadAccepted_Status": {"OK"}, "PayloadTerminal_Status": {"OK"},
        "SenderComplete_Status": {"OK"}, "GroundReceipt_Status": {"OK"},
        "CommandComplete_Status": {"OK"},
    }, timeout=timeout)
    return {
        "label": label, "verification_token": token, "command": command,
        "accepted": accepted, "terminal": terminal, "stored": stored,
        "committed": committed, "sender_started": started,
        "sender_complete": sender_complete, "verified_downlink": verified_downlink,
        "ground_receipt_event": receipt_event, "ground_receipt": receipt,
        "downloaded_raw_path": raw_path, "product_id": product_id,
        "product_bytes": byte_count, "source_crc": source_crc,
        "transfer_id": transfer_id, "history": history,
    }


def start_verified_capture(token, duration, timeout=DEFAULT_TIMEOUT):
    command = issue(f"{SCIENCE}.VERIFIED_COLLECT_AND_DOWNLINK", {
        "verificationToken": token, "durationMinutes": duration,
    })
    accepted = capture_status(command, token, "ACCEPTED", timeout=timeout)
    return command, accepted


def random_token(used=()):
    used = set(used)
    while True:
        token = secrets.randbelow(0xFFFFFFFF) + 1
        if token not in used:
            return token


def collect_catalog(command, product_ids, timeout=DEFAULT_TIMEOUT):
    expected_ids = {int(product_id) for product_id in product_ids}
    released = stamp(command["generationTime"])
    deadline = time.monotonic() + timeout
    found = {}
    while time.monotonic() < deadline:
        for event in events("StorageCatalogEntry"):
            if event.get("type") != f"{DEPLOYMENT}.storageManager.StorageCatalogEntry":
                continue
            if stamp(event["receptionTime"]) < released:
                continue
            product_id = int(event.get("extra", {}).get("productId", -1))
            if product_id in expected_ids:
                found[product_id] = event
        if expected_ids.issubset(found):
            return found
        time.sleep(POLL_SECONDS)
    raise RuntimeError(f"REPORT_STORAGE_HISTORY omitted product IDs {sorted(expected_ids - found.keys())}; got {found}")


def report_catalog(expected_ids=(), timeout=DEFAULT_TIMEOUT, expected_count=None):
    expected_ids = set(map(int, expected_ids))
    if expected_count is not None and int(expected_count) < 0:
        raise ValueError(f"expected_count must be non-negative, got {expected_count}")
    if expected_ids and expected_count is not None and len(expected_ids) != int(expected_count):
        raise ValueError(f"expected_ids has {len(expected_ids)} IDs but expected_count is {expected_count}")
    target_count = int(expected_count) if expected_count is not None else (
        len(expected_ids) if expected_ids else None)
    if target_count is None:
        raise ValueError("report_catalog requires expected_ids or an exact expected_count")
    deadline = time.monotonic() + max(0.0, float(timeout))
    max_attempts = 3
    max_attempt_window = 5.0
    attempts = []
    catalog_type = f"{DEPLOYMENT}.storageManager.StorageCatalogEntry"
    empty_type = f"{DEPLOYMENT}.storageManager.StorageHistoryEmpty"

    def metadata_errors_for(event, index):
        fields = event.get("extra", {})
        errors = []
        for name in ("productId", "productBytes", "sourceCrc", "sourcePath"):
            if fields.get(name) in (None, ""):
                errors.append(f"event[{index}] missing {name}")
        source_path = fields.get("sourcePath")
        if source_path not in (None, "") and (
                not isinstance(source_path, str) or not source_path.strip()):
            errors.append(f"event[{index}] invalid sourcePath={source_path!r}")
        for name, minimum, maximum in (("productId", 1, 0xFFFFFFFF),
                                       ("productBytes", 1, 0xFFFFFFFF),
                                       ("sourceCrc", 0, 0xFFFFFFFF)):
            if fields.get(name) in (None, ""):
                continue
            try:
                value = int(fields[name], 0) if isinstance(fields[name], str) else int(fields[name])
            except (TypeError, ValueError):
                errors.append(f"event[{index}] invalid {name}={fields[name]!r}")
                continue
            if not minimum <= value <= maximum:
                errors.append(f"event[{index}] out-of-range {name}={value}")
        return errors

    for attempt_number in range(1, max_attempts + 1):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        attempt_window = min(max_attempt_window, remaining / (max_attempts - attempt_number + 1))
        attempt_started = time.monotonic()
        command = issue(f"{STORAGE}.REPORT_STORAGE_HISTORY")
        after = stamp(command["generationTime"])
        attempt_deadline = min(deadline, time.monotonic() + attempt_window)
        latest = {
            "catalog_events": [], "empty_events": [], "product_ids": [],
            "missing_product_ids": sorted(expected_ids), "unexpected_product_ids": [],
            "duplicate_product_ids": [], "metadata_errors": [], "complete": False,
        }
        while True:
            rows = [event for event in events("StorageCatalogEntry")
                    if event.get("type") == catalog_type
                    and event.get("receptionTime")
                    and stamp(event["receptionTime"]) >= after]
            empty_rows = [event for event in events("StorageHistoryEmpty")
                          if event.get("type") == empty_type
                          and event.get("receptionTime")
                          and stamp(event["receptionTime"]) >= after]
            found = {}
            ids = []
            invalid_id_rows = []
            metadata_errors = []
            for index, event in enumerate(rows):
                fields = event.get("extra", {})
                try:
                    product_id = int(fields.get("productId", -1))
                except (TypeError, ValueError):
                    product_id = -1
                if product_id <= 0:
                    invalid_id_rows.append(index)
                else:
                    ids.append(product_id)
                    found[product_id] = event
                metadata_errors.extend(metadata_errors_for(event, index))
            unique_ids = set(ids)
            duplicates = sorted(product_id for product_id in unique_ids if ids.count(product_id) > 1)
            missing = sorted(expected_ids - unique_ids)
            unexpected = sorted(unique_ids - expected_ids) if expected_ids else []
            if invalid_id_rows:
                metadata_errors.extend(f"event[{index}] invalid productId" for index in invalid_id_rows)
            count_matches = len(rows) == target_count and len(unique_ids) == target_count
            ids_match = not expected_ids or unique_ids == expected_ids
            if target_count == 0:
                complete = not rows and bool(empty_rows)
            else:
                complete = count_matches and ids_match
            complete = complete and not metadata_errors
            latest = {
                "catalog_events": rows,
                "empty_events": empty_rows,
                "event_count": len(rows),
                "product_ids": sorted(unique_ids),
                "missing_product_ids": missing,
                "unexpected_product_ids": unexpected,
                "duplicate_product_ids": duplicates,
                "invalid_id_event_indexes": invalid_id_rows,
                "metadata_errors": metadata_errors,
                "expected_count": target_count,
                "count_matches": count_matches,
                "expected_product_ids": sorted(expected_ids),
                "ids_match": ids_match,
                "complete": complete,
            }
            if complete or time.monotonic() >= attempt_deadline:
                break
            time.sleep(min(POLL_SECONDS, max(0.0, attempt_deadline - time.monotonic())))

        attempt = {
            "attempt": attempt_number,
            "command": dict(command),
            "generation_time": command.get("generationTime"),
            "elapsed_seconds": round(time.monotonic() - attempt_started, 3),
            "status": "PASS" if latest["complete"] else "INCOMPLETE",
            **latest,
        }
        attempts.append(attempt)
        if latest["complete"]:
            # Only a single fresh REPORT_STORAGE_HISTORY attempt can satisfy
            # the count and metadata contract; partial reports are never merged.
            command["report_attempts"] = attempts
            command["successful_report_attempt"] = attempt_number
            return command, {int(event["extra"]["productId"]): event
                             for event in latest["catalog_events"]}

    raise RuntimeError(
        "REPORT_STORAGE_HISTORY did not produce one complete fresh catalog report; "
        f"expected_count={target_count}, expected_ids={sorted(expected_ids)}, "
        f"attempts={json.dumps(attempts, sort_keys=True)}"
    )


def select_product(product_id, expected_entry=None, timeout=DEFAULT_TIMEOUT):
    command = issue(f"{STORAGE}.SELECT_PRODUCT", {"productId": int(product_id)})
    selected = returned(command, "storageManager", "ProductSelected",
                         lambda event: int(event["extra"].get("productId", -1)) == int(product_id),
                         timeout=timeout)
    if expected_entry is not None:
        for key in ("productBytes", "sourceCrc"):
            if int(selected["extra"].get(key, -1)) != int(expected_entry[key]):
                raise RuntimeError(f"Selected product {product_id} {key} disagrees with catalog: {selected}")
    selected_value = fresh_parameter(f"{STORAGE}.SelectedProductId", stamp(command["generationTime"]),
                                     predicate=lambda value: numeric(value) == int(product_id))
    return {"command": command, "event": selected, "telemetry": selected_value}


def verified_selected_downlink(current, product_record, timeout=DEFAULT_TIMEOUT):
    token = random_token()
    product_id = int(product_record["product_id"])
    command = issue(f"{COMMS}.VERIFIED_REQUEST_SCIENCE_DOWNLINK",
                    {"verificationToken": token})
    admitted = downlink_status(command, token, "DOWNLINKING", timeout=timeout)
    if (int(admitted["extra"].get("productId", -1)) != product_id
            or int(admitted["extra"].get("productBytes", -1)) != int(product_record["product_bytes"])
            or int(admitted["extra"].get("sourceCrc", -1)) != int(product_record["source_crc"])
            or int(admitted["extra"].get("transferId", -1)) <= 0):
        raise RuntimeError(f"Selected-product admission identity mismatch: {admitted}")
    started = returned(command, "payloadDownlinkApp", "PayloadDownlinkStarted",
                       lambda event: int(event["extra"].get("productId", -1)) == product_id,
                       timeout=timeout)
    transfer_id = int(started["extra"]["transferId"])
    if (int(started["extra"].get("byteCount", -1)) != int(product_record["product_bytes"])
            or transfer_id != int(admitted["extra"]["transferId"])):
        raise RuntimeError(f"Selected-product sender start disagrees with Comms admission: {started}")
    released = stamp(command["generationTime"])
    sender_complete = wait_named_event(
        "PayloadDownlinkComplete", released, f"{DEPLOYMENT}.payloadDownlinkApp.PayloadDownlinkComplete",
        lambda event: int(event.get("extra", {}).get("transferId", -1)) == transfer_id,
        timeout=timeout)
    verified_downlink = downlink_status(command, token, "DONE", timeout=timeout)
    if (int(verified_downlink["extra"].get("productId", -1)) != product_id
            or int(verified_downlink["extra"].get("transferId", -1)) != transfer_id):
        raise RuntimeError(f"Selected-product downlink status identity mismatch: {verified_downlink}")
    receipt_event = wait_named_event(
        "GroundScienceReceiptVerified", released, "GroundScienceReceiptVerified",
        lambda event: extra(event, "runId") == current["local_run_id"]
        and extra(event, "productId") == str(product_id)
        and extra(event, "transferId") == str(transfer_id), timeout=timeout)
    receipt = verify_raw_receipt(
        receipt_event, current["local_run_id"], expected_product_id=product_id,
        expected_transfer_id=transfer_id,
        expected_digest=product_record["ground_receipt"]["sha256"],
        expected_crc=product_record["source_crc"], expected_size=product_record["product_bytes"])
    raw_path = save_ground_bytes(current, product_id, receipt.pop("raw"), receipt["sha256"])
    history = status_history(command, {
        "PayloadAccepted_Status": {"OK"}, "SenderComplete_Status": {"OK"},
        "GroundReceipt_Status": {"OK"},
        "CommandComplete_Status": {"OK"},
    }, timeout=timeout)
    return {"verification_token": token, "command": command,
            "verified_downlink_admitted": admitted,
            "sender_started": started, "sender_complete": sender_complete,
            "verified_downlink": verified_downlink,
            "ground_receipt_event": receipt_event, "ground_receipt": receipt,
            "downloaded_raw_path": raw_path, "transfer_id": transfer_id,
            "history": history}


def reject_reused_downlink_token(current, successful_downlink, product_record,
                                 timeout=DEFAULT_TIMEOUT):
    """Prove a repeated token is rejected without mutating its successful row."""
    original_command = successful_downlink["command"]
    token = int(successful_downlink["verification_token"])
    original_before = history_entry(original_command)
    require_history_values(original_before, {
        "PayloadAccepted_Status": {"OK"}, "SenderComplete_Status": {"OK"},
        "GroundReceipt_Status": {"OK"}, "CommandComplete_Status": {"OK"},
    })
    command = issue(f"{COMMS}.VERIFIED_REQUEST_SCIENCE_DOWNLINK",
                    {"verificationToken": token})
    rejected = downlink_status(command, token, "ERROR", timeout=timeout)
    if (int(rejected["extra"].get("productId", -1)) != 0
            or int(rejected["extra"].get("transferId", -1)) != 0):
        raise RuntimeError(f"Reused downlink token was not rejected before transfer admission: {rejected}")
    history = status_history(command, {
        "PayloadAccepted_Status": {"NOK"}, "SenderComplete_Status": {"NOK"},
        "CommandComplete_Status": {"NOK"},
    }, timeout=timeout)
    assert_no_command_success(command)
    starts = event_matches(
        "PayloadDownlinkStarted", stamp(command["generationTime"]),
        f"{DEPLOYMENT}.payloadDownlinkApp.PayloadDownlinkStarted",
        lambda event: int(event.get("extra", {}).get("productId", -1))
        == int(product_record["product_id"]))
    if starts:
        raise RuntimeError(f"Reused downlink token started another transfer: {starts}")
    original_after = history_entry(original_command)
    require_history_values(original_after, {
        "PayloadAccepted_Status": {"OK"}, "SenderComplete_Status": {"OK"},
        "GroundReceipt_Status": {"OK"}, "CommandComplete_Status": {"OK"},
    })
    if snapshot_history(original_before) != snapshot_history(original_after):
        raise RuntimeError("Reusing a completed downlink token mutated the original command-history row")
    return {"command": command, "rejected_status": rejected, "history": history,
            "original_command_after_reuse": original_after}


def evidence_path(current, phase):
    return Path(current["run_dir"]) / f"completion-storage-{phase}-{secrets.token_hex(6)}.json"


def write_evidence(path, result):
    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"result": result.get("result"), "evidence": str(path)}, indent=2), flush=True)


def assert_base_and_storage(current):
    wait_for_running()
    started = stamp(current["started_at"])
    mode = mode_value(acquired_after=started)
    if mode != "BASE":
        raise RuntimeError(f"Acceptance requires an idle BASE stack; Yamcs reports {mode}")
    ready, ready_value = storage_ready(started)
    if not ready:
        raise RuntimeError(f"Yamcs StorageReady is false: {ready_value}")
    return {"started_at": current["started_at"], "mode": mode,
            "storage_ready": ready_value}


def initial_negative_probes(current, timeout):
    result = []
    initial_count = storage_count()
    result.append(reject_capture(current, 0, 1, "INVALID", timeout=timeout))
    result.append(reject_capture(current, random_token(), 0, "INVALID", timeout=timeout))

    configured = configure_simulation("FAIL", delay_ms=0, timeout_ms=8000)
    failed = reject_capture(current, random_token(), 1, "FAILED", timeout=timeout)
    failed["configuration"] = configured
    result.append(failed)

    configured = configure_simulation("DELAYED_RESPONSE", delay_ms=6000, timeout_ms=1000)
    old_command, old_accepted = start_verified_capture(random_token(), 1, timeout=timeout)
    old_token = int(old_accepted["extra"]["verificationToken"])
    old_terminal = capture_status(old_command, old_token, "TIMED_OUT", timeout=timeout)
    old_history = status_history(old_command, {
        "PayloadAccepted_Status": {"OK"}, "PayloadTerminal_Status": {"TIMEOUT"},
        "CommandComplete_Status": {"NOK"},
    }, timeout=timeout)
    assert_no_command_success(old_command)
    timeout_probe = {"configuration": configured, "command": old_command,
                     "accepted": old_accepted, "terminal": old_terminal,
                     "history": old_history, "stored_products_before": initial_count}

    # Start a newer pending operation while the old delayed callback is still
    # due. An old legacy request ID must not affect the verified owner's
    # generation when the manager reports generation-zero BUSY.
    configured = configure_simulation("DELAYED_RESPONSE", delay_ms=6000, timeout_ms=8000)
    new_token = random_token({old_token})
    new_command, new_accepted = start_verified_capture(new_token, 1, timeout=timeout)
    owner_request_id = int(new_accepted["extra"]["requestId"])
    owner_generation = int(new_accepted["extra"]["generation"])
    legacy_contender = issue(f"{MANAGER}.START_PAYLOAD_COLLECTION",
                             {"collectionId": owner_request_id})
    legacy_busy = returned(
        legacy_contender, "payloadManager", "PayloadOperationStatus",
        lambda event: extra(event, "requestId") == str(owner_request_id)
        and extra(event, "generation") == "0" and extra(event, "state") == "BUSY"
        and extra(event, "kind") in {"CAPTURE", "1"}, timeout=timeout)
    if stamp(legacy_busy["receptionTime"]) <= stamp(new_accepted["receptionTime"]):
        raise RuntimeError("Legacy same-ID contender was not observed after its verified owner admission")
    busy = reject_capture(current, random_token({old_token, new_token}), 1, "BUSY", timeout=timeout)
    busy["while_command_id"] = new_command.get("id")
    busy["configuration"] = configured
    busy["same_request_id_legacy_contender"] = {
        "command": legacy_contender, "status": legacy_busy,
        "owner_request_id": owner_request_id, "owner_generation": owner_generation,
    }
    result.append({"timeout": timeout_probe, "busy": busy})

    old_request_id = int(old_accepted["extra"]["requestId"])
    old_generation = int(old_accepted["extra"]["generation"])
    ignored = wait_named_event(
        "PayloadReplyIgnored", stamp(new_accepted["receptionTime"]),
        f"{MANAGER}.PayloadReplyIgnored",
        lambda event: extra(event, "requestId") == str(old_request_id)
        and extra(event, "generation") == str(old_generation), timeout=timeout)
    new_terminal_before_late = [event for event in events("VerifiedCaptureStatus")
                                if event.get("type") == f"{SCIENCE}.VerifiedCaptureStatus"
                                and extra(event, "verificationToken") == str(new_token)
                                and extra(event, "state") == "COMPLETED"
                                and stamp(new_accepted["receptionTime"]) <= stamp(event["receptionTime"])
                                <= stamp(ignored["receptionTime"])]
    if new_terminal_before_late:
        raise RuntimeError(f"New operation completed before its own delayed response: {new_terminal_before_late}")
    new_product = wait_capture_delivery(current, new_command, new_token, accepted=new_accepted,
                                        timeout=timeout, label="late-reply owner")
    if (int(new_product["terminal"]["extra"].get("requestId", -1)) != owner_request_id
            or int(new_product["terminal"]["extra"].get("generation", -1)) != owner_generation):
        raise RuntimeError("Same-ID BUSY contender changed the accepted owner's identity")
    if stamp(new_product["terminal"]["receptionTime"]) <= stamp(ignored["receptionTime"]):
        raise RuntimeError("Late old reply was not observed before the new operation completed")
    old_after_late = history_entry(old_command)
    require_history_values(old_after_late, {
        "PayloadAccepted_Status": {"OK"}, "PayloadTerminal_Status": {"TIMEOUT"},
        "CommandComplete_Status": {"NOK"},
    })
    result.append({"late_reply_owner": new_product,
                   "old_reply_ignored": ignored,
                   "old_timeout_history_after_late_reply": snapshot_history(old_after_late)})

    successful_owner_before_reuse = history_entry(new_command)
    require_history_values(successful_owner_before_reuse, {
        "PayloadAccepted_Status": {"OK"}, "PayloadTerminal_Status": {"OK"},
        "SenderComplete_Status": {"OK"}, "GroundReceipt_Status": {"OK"},
        "CommandComplete_Status": {"OK"},
    })
    reused_token = reject_capture(current, new_token, 1, "INVALID", timeout=timeout)
    successful_owner_after_reuse = history_entry(new_command)
    require_history_values(successful_owner_after_reuse, {
        "PayloadAccepted_Status": {"OK"}, "PayloadTerminal_Status": {"OK"},
        "SenderComplete_Status": {"OK"}, "GroundReceipt_Status": {"OK"},
        "CommandComplete_Status": {"OK"},
    })
    if snapshot_history(successful_owner_before_reuse) != snapshot_history(successful_owner_after_reuse):
        raise RuntimeError("Reusing a completed capture token mutated the original command-history row")
    reused_token["reused_completed_token"] = new_token
    reused_token["original_successful_command_after_reuse"] = successful_owner_after_reuse
    result.append(reused_token)
    configure_simulation("NORMAL", delay_ms=0, timeout_ms=8000)
    if storage_count() != initial_count + 1:
        raise RuntimeError(f"Failure/rejection probes changed product count unexpectedly: {initial_count} -> {storage_count()}")
    return result, new_product


def before_restart(args, current, result):
    result["stack"] = assert_base_and_storage(current)
    if Path(args.manifest).exists():
        raise RuntimeError(f"Refusing to overwrite existing acceptance manifest: {args.manifest}")
    initial_count = storage_count()
    if initial_count != 0:
        raise RuntimeError(f"Use an empty isolated storage root for before-restart; StoredProducts={initial_count}")
    negative, first = initial_negative_probes(current, args.timeout)
    result["negative_checks"] = negative
    second_token = random_token({first["verification_token"]})
    second_command, second_accepted = start_verified_capture(second_token, 2, timeout=args.timeout)
    second = wait_capture_delivery(current, second_command, second_token,
                                   accepted=second_accepted, timeout=args.timeout,
                                   label="second durable product")
    products = [first, second]
    product_ids = [record["product_id"] for record in products]
    object_names = [record["ground_receipt"]["raw_object"] for record in products]
    if len(set(product_ids)) != 2 or len(set(object_names)) != 2:
        raise RuntimeError(f"Two accepted captures did not produce unique product/object IDs: {products}")
    if storage_count() != initial_count + 2:
        raise RuntimeError(f"Expected exactly two committed products, got StoredProducts={storage_count()}")
    catalog_command, catalog = report_catalog(product_ids, timeout=args.timeout)
    for product in products:
        entry = catalog[product["product_id"]]
        if (int(entry["extra"].get("productBytes", -1)) != product["product_bytes"]
                or int(entry["extra"].get("sourceCrc", -1)) != product["source_crc"]):
            raise RuntimeError(f"Yamcs storage catalog does not match receipt for product {product['product_id']}: {entry}")
    manifest = {
        "schema": "n2-completion-storage-v1",
        "result": "PASS",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "storage_root": current["storage_root"],
        "before_restart": {
            "run_id": current["local_run_id"], "started_at": current["started_at"],
            "binary_sha256": current.get("binary_sha256"),
            "dictionary_sha256": current.get("dictionary_sha256"),
            "catalog_command": catalog_command,
            "products": products,
        },
    }
    Path(args.manifest).parent.mkdir(parents=True, exist_ok=True)
    Path(args.manifest).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    result["manifest"] = str(args.manifest)
    result["products"] = products
    result["catalog"] = catalog
    result["result"] = "PASS"


def load_manifest(path):
    manifest = json.loads(Path(path).read_text())
    if manifest.get("schema") != "n2-completion-storage-v1" or manifest.get("result") != "PASS":
        raise RuntimeError(f"Not a successful completion/storage manifest: {path}")
    products = manifest.get("before_restart", {}).get("products", [])
    if len(products) != 2:
        raise RuntimeError(f"Manifest must record exactly two pre-restart products; found {len(products)}")
    return manifest


def manifest_products(manifest):
    products = list(manifest.get("before_restart", {}).get("products", []))
    post_restart = manifest.get("after_restart", {}).get("new_product")
    if post_restart is not None:
        known_ids = {int(product["product_id"]) for product in products}
        if int(post_restart["product_id"]) not in known_ids:
            products.append(post_restart)
    return products


def after_restart(args, current, result):
    manifest = load_manifest(args.manifest)
    if current["storage_root"] != manifest["storage_root"]:
        raise RuntimeError(f"Storage root changed across restart: {manifest['storage_root']} -> {current['storage_root']}")
    if current["local_run_id"] == manifest["before_restart"]["run_id"]:
        raise RuntimeError("Local receiver run ID did not change across restart")
    result["stack"] = assert_base_and_storage(current)
    products = manifest["before_restart"]["products"]
    product_ids = [int(product["product_id"]) for product in products]
    catalog_command, catalog = report_catalog(product_ids, timeout=args.timeout)
    recovered = {}
    for product in products:
        product_id = int(product["product_id"])
        entry = catalog[product_id]
        if (int(entry["extra"].get("productBytes", -1)) != int(product["product_bytes"])
                or int(entry["extra"].get("sourceCrc", -1)) != int(product["source_crc"])):
            raise RuntimeError(f"Recovered catalog metadata changed for product {product_id}: {entry}")
        recovered_event = wait_named_event(
            "ScienceProductRecovered", stamp(current["started_at"]),
            f"{STORAGE}.ScienceProductRecovered",
            lambda event, product_id=product_id: int(event.get("extra", {}).get("productId", -1)) == product_id
            and int(event.get("extra", {}).get("productBytes", -1)) == int(product["product_bytes"])
            and int(event.get("extra", {}).get("sourceCrc", -1)) == int(product["source_crc"]),
            timeout=args.timeout)
        recovered[str(product_id)] = recovered_event
    selected_product = min(products, key=lambda product: product["product_id"])
    selected = select_product(selected_product["product_id"], {
        "productBytes": selected_product["product_bytes"],
        "sourceCrc": selected_product["source_crc"],
    }, timeout=args.timeout)
    old_downlink = verified_selected_downlink(current, selected_product, timeout=args.timeout)
    if (old_downlink["ground_receipt"]["sha256"]
            != selected_product["ground_receipt"]["sha256"]):
        raise RuntimeError("Selected older product downlink bytes differ from its pre-restart Yamcs receipt")
    reused_downlink_token = reject_reused_downlink_token(
        current, old_downlink, selected_product, timeout=args.timeout)

    third_token = random_token(product["verification_token"] for product in products)
    third_command, third_accepted = start_verified_capture(third_token, 1, timeout=args.timeout)
    third = wait_capture_delivery(current, third_command, third_token, accepted=third_accepted,
                                  timeout=args.timeout, label="post-restart product")
    if third["product_id"] in product_ids:
        raise RuntimeError(f"Post-restart product ID overwrote a prior product: {third['product_id']}")
    if third["ground_receipt"]["raw_object"] in {
            product["ground_receipt"]["raw_object"] for product in products}:
        raise RuntimeError("Post-restart product reused a Yamcs ground object name")
    all_ids = [*product_ids, third["product_id"]]
    if len(set(all_ids)) != len(all_ids):
        raise RuntimeError(f"Product identity was overwritten after restart: {all_ids}")
    manifest["after_restart"] = {
        "run_id": current["local_run_id"], "started_at": current["started_at"],
        "binary_sha256": current.get("binary_sha256"),
        "dictionary_sha256": current.get("dictionary_sha256"),
        "catalog_command": catalog_command, "catalog": catalog,
        "recovered_events": recovered, "selected_older_product": selected,
        "selected_older_downlink": old_downlink,
        "reused_downlink_token": reused_downlink_token, "new_product": third,
    }
    manifest["result"] = "PASS"
    Path(args.manifest).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    result.update({"result": "PASS", "manifest": str(args.manifest),
                  "catalog": catalog, "recovered_events": recovered,
                  "selected_older_product": selected,
                  "selected_older_downlink": old_downlink,
                  "reused_downlink_token": reused_downlink_token,
                  "new_product": third})


def missing_receipt(args, current, result):
    if current.get("local_science_format") != "pulses-v1":
        raise RuntimeError("missing-receipt phase requires the pulse stack from run_pulses.sh")
    profile = current.get("local_science_fault_profile", [])
    if "withhold-receipt" not in profile:
        raise RuntimeError(f"Stack was not started with withhold-receipt fault profile: {profile}")
    configured_timeout = int(current.get("verification_timeout_seconds", 0))
    if configured_timeout <= 0:
        raise RuntimeError("Stack metadata omits its positive verification timeout")
    result["stack"] = assert_base_and_storage(current)
    token = random_token()
    command, accepted = start_verified_capture(token, 2,
                                               timeout=max(args.timeout, configured_timeout + 20))
    terminal = capture_status(command, token, "COMPLETED", timeout=max(args.timeout, configured_timeout + 20))
    product_id = int(terminal["extra"]["productId"])
    byte_count = int(terminal["extra"]["productBytes"])
    source_crc = int(terminal["extra"]["sourceCrc"])
    committed = returned(command, "storageManager", "ScienceProductCommitted",
                         lambda event: int(event["extra"].get("productId", -1)) == product_id
                         and int(event["extra"].get("productBytes", -1)) == byte_count
                         and int(event["extra"].get("sourceCrc", -1)) == source_crc,
                         timeout=args.timeout)
    started = returned(command, "payloadDownlinkApp", "PayloadDownlinkStarted",
                       lambda event: int(event["extra"].get("productId", -1)) == product_id,
                       timeout=args.timeout)
    transfer_id = int(started["extra"]["transferId"])
    sender_complete = wait_named_event(
        "PayloadDownlinkComplete", stamp(command["generationTime"]),
        f"{DEPLOYMENT}.payloadDownlinkApp.PayloadDownlinkComplete",
        lambda event: int(event.get("extra", {}).get("transferId", -1)) == transfer_id,
        timeout=args.timeout)
    ground_timeout = max(args.timeout, configured_timeout + 20)
    history = status_history(command, {
        "PayloadAccepted_Status": {"OK"}, "PayloadTerminal_Status": {"OK"},
        "SenderComplete_Status": {"OK"}, "GroundReceipt_Status": {"TIMEOUT"},
        "CommandComplete_Status": {"NOK"},
    }, timeout=ground_timeout)
    assert_no_command_success(command)
    ground_events = event_matches("GroundScienceReceiptVerified", stamp(command["generationTime"]),
                                  "GroundScienceReceiptVerified",
                                  lambda event: extra(event, "runId") == current["local_run_id"]
                                  and extra(event, "productId") == str(product_id)
                                  and extra(event, "transferId") == str(transfer_id))
    if ground_events:
        raise RuntimeError(f"Withheld-receipt fault unexpectedly produced ground success: {ground_events}")
    result.update({"result": "PASS", "verification_token": token,
                   "command": command, "accepted": accepted, "terminal": terminal,
                   "committed": committed, "sender_started": started,
                   "sender_complete": sender_complete,
                   "history": history, "ground_receipt_events": ground_events,
                   "configured_verification_timeout_seconds": configured_timeout,
                   "verified_product_bytes": byte_count, "verified_product_crc": source_crc,
                   "product_id": product_id, "transfer_id": transfer_id})


def storage_fault(args, current, result):
    manifest = load_manifest(args.manifest)
    faults = {name: getattr(args, name) for name in
              ("corrupt_product", "missing_product", "incomplete_product")
              if getattr(args, name) is not None}
    bad_ids = [int(product_id) for product_id in faults.values()]
    if not bad_ids and not args.full_catalog:
        raise RuntimeError("storage-fault requires one or more product fault IDs or --full-catalog")
    if len(set(bad_ids)) != len(bad_ids) or any(product_id <= 0 for product_id in bad_ids):
        raise RuntimeError(f"Fault product IDs must be distinct positive IDs: {bad_ids}")
    result["stack"] = assert_base_and_storage(current)
    ready, ready_value = storage_ready(stamp(current["started_at"]))
    if not ready:
        raise RuntimeError(f"Faulted record made the whole store unavailable: {ready_value}")
    for fault_name, product_id in faults.items():
        event = wait_named_event(
            "ScienceProductRejected", stamp(current["started_at"]),
            f"{STORAGE}.ScienceProductRejected",
            lambda item, product_id=int(product_id): int(item.get("extra", {}).get("productId", -1)) == product_id,
            timeout=args.timeout)
        result.setdefault("rejected_records", []).append({"fault": fault_name, "product_id": int(product_id),
                                                            "event": event})
    known_products = manifest_products(manifest)
    known_ids = {int(product["product_id"]) for product in known_products}
    explicit_good = set(args.expect_product or [])
    expected_pool = explicit_good if args.full_catalog else (known_ids | explicit_good)
    expected_good = expected_pool - set(bad_ids)
    if not expected_good:
        raise RuntimeError("Provide at least one non-faulted product ID with --expect-product")
    catalog_count = storage_count()
    catalog_command, catalog = report_catalog(expected_count=catalog_count, timeout=args.timeout)
    if not expected_good.issubset(catalog):
        raise RuntimeError(f"Valid expected products missing from recovered catalog: {expected_good - set(catalog)}")
    if set(bad_ids).intersection(catalog):
        raise RuntimeError(f"Faulted products were published in recovered catalog: {set(bad_ids).intersection(catalog)}")
    for product in known_products:
        product_id = int(product["product_id"])
        if product_id in set(bad_ids):
            if product_id in catalog:
                raise RuntimeError(f"Faulted product {product_id} was published in recovered catalog")
            continue
        if product_id in catalog:
            entry = catalog[product_id]
            if (int(entry["extra"].get("productBytes", -1)) != int(product["product_bytes"])
                    or int(entry["extra"].get("sourceCrc", -1)) != int(product["source_crc"])):
                raise RuntimeError(f"Valid product {product_id} changed in catalog: {entry}")
    for product_id in bad_ids:
        command = issue(f"{STORAGE}.SELECT_PRODUCT", {"productId": product_id})
        rejected = returned(command, "storageManager", "ScienceProductRejected",
                            lambda event, product_id=product_id:
                            int(event["extra"].get("productId", -1)) == product_id,
                            timeout=args.timeout)
        selected = event_matches("ProductSelected", stamp(command["generationTime"]),
                                 f"{STORAGE}.ProductSelected",
                                 lambda event, product_id=product_id:
                                 int(event["extra"].get("productId", -1)) == product_id)
        if selected:
            raise RuntimeError(f"Storage selected rejected product {product_id}: {selected}")
        result.setdefault("selection_rejections", []).append({"product_id": product_id,
                                                                 "command": command,
                                                                 "rejected": rejected})
    safe_id = min(expected_good)
    safe_selection = select_product(safe_id, timeout=args.timeout)
    known_safe_record = next((product for product in known_products
                              if int(product["product_id"]) == safe_id), None)
    safe_downlink = None
    if known_safe_record is not None:
        safe_downlink = verified_selected_downlink(current, known_safe_record, timeout=args.timeout)
        if safe_downlink["ground_receipt"]["sha256"] != known_safe_record["ground_receipt"]["sha256"]:
            raise RuntimeError(f"Recovered intact product {safe_id} downlink differs from its manifest hash")
    result["reselected_valid_product"] = {
        "product_id": safe_id, "selection": safe_selection,
        "recorded_manifest_product": known_safe_record is not None,
        "verified_downlink": safe_downlink,
    }
    if args.minimum_new_product_id is not None:
        before_write_ids = set(catalog)
        before_write_count = storage_count()
        token = random_token(product["verification_token"] for product in known_products)
        command, accepted = start_verified_capture(token, 1, timeout=args.timeout)
        new_product = wait_capture_delivery(current, command, token, accepted=accepted,
                                            timeout=args.timeout, label="write after storage faults")
        product_id = int(new_product["product_id"])
        if product_id < args.minimum_new_product_id:
            raise RuntimeError(f"Recovered allocator reused a reserved/orphan ID: {product_id} < {args.minimum_new_product_id}")
        if product_id in set(bad_ids) or product_id in before_write_ids:
            raise RuntimeError(f"New product ID {product_id} collided with a rejected or existing catalog ID")
        if storage_count() != before_write_count + 1:
            raise RuntimeError(f"Post-fault capture did not add exactly one product: {before_write_count} -> {storage_count()}")
        write_catalog_command, write_catalog = report_catalog(expected_count=storage_count(),
                                                              timeout=args.timeout)
        expected_after_ids = before_write_ids | {product_id}
        if set(write_catalog) != expected_after_ids:
            raise RuntimeError(f"Post-fault write changed catalog unexpectedly: expected {expected_after_ids}, got {set(write_catalog)}")
        for old_id in before_write_ids:
            old_record = next((product for product in known_products
                               if int(product["product_id"]) == old_id), None)
            if old_record is not None:
                old_entry = write_catalog[old_id]
                if (int(old_entry["extra"].get("productBytes", -1)) != int(old_record["product_bytes"])
                        or int(old_entry["extra"].get("sourceCrc", -1)) != int(old_record["source_crc"])):
                    raise RuntimeError(f"Post-fault write altered intact product {old_id}: {old_entry}")
        new_entry = write_catalog[product_id]
        if (int(new_entry["extra"].get("productBytes", -1)) != int(new_product["product_bytes"])
                or int(new_entry["extra"].get("sourceCrc", -1)) != int(new_product["source_crc"])):
            raise RuntimeError(f"Post-fault catalog entry disagrees with new receipt: {new_entry}")
        result["post_fault_write"] = {
            "command": command, "accepted": accepted, "product": new_product,
            "minimum_product_id": args.minimum_new_product_id,
            "catalog_command": write_catalog_command, "catalog": write_catalog,
            "preserved_product_ids": sorted(before_write_ids),
        }
    if args.full_catalog:
        before_count = storage_count()
        if before_count != MAX_STORAGE_PRODUCTS:
            raise RuntimeError(f"Full-catalog setup expected {MAX_STORAGE_PRODUCTS} entries, got {before_count}")
        before_catalog_command, before_catalog = report_catalog(expected_count=MAX_STORAGE_PRODUCTS,
                                                                timeout=args.timeout)
        before_ids = set(before_catalog)
        token = random_token()
        command, accepted = start_verified_capture(token, 1, timeout=args.timeout)
        terminal = capture_status(command, token, "COMPLETED", timeout=args.timeout)
        candidate_id = int(terminal["extra"]["productId"])
        if candidate_id in before_ids:
            raise RuntimeError(f"Full-storage candidate reused product ID {candidate_id}")
        rejected = returned(command, "storageManager", "ScienceProductRejected",
                            lambda event: int(event["extra"].get("productId", -1)) == candidate_id,
                            timeout=args.timeout)
        history = status_history(command, {
            "PayloadAccepted_Status": {"OK"}, "PayloadTerminal_Status": {"OK"},
            "SenderComplete_Status": {"NOK"}, "CommandComplete_Status": {"NOK"},
        }, timeout=max(args.timeout, int(current.get("verification_timeout_seconds", 0)) + 5))
        if storage_count() != before_count:
            raise RuntimeError(f"Full storage discarded/overwrote a product: {before_count} -> {storage_count()}")
        after_command, after_catalog = report_catalog(expected_count=MAX_STORAGE_PRODUCTS,
                                                      timeout=args.timeout)
        if set(after_catalog) != before_ids:
            raise RuntimeError(f"Full storage catalog changed after rejection: {before_ids} -> {set(after_catalog)}")
        result["full_catalog"] = {"before_report": before_catalog_command,
                                  "command": command, "accepted": accepted,
                                  "terminal": terminal, "rejection": rejected,
                                  "history": history, "after_report": after_command,
                                  "product_ids": sorted(before_ids)}
    result.update({"result": "PASS", "manifest": str(args.manifest),
                   "storage_root": current["storage_root"],
                   "catalog_command": catalog_command, "catalog": catalog,
                   "fault_ids": faults})


def verifier_restart(args, current, result):
    if not args.ready_file or not args.continue_file:
        raise RuntimeError("verifier-restart requires --ready-file and --continue-file for the external process-restart orchestrator")
    ready_file = Path(args.ready_file)
    continue_file = Path(args.continue_file)
    if ready_file.resolve() == continue_file.resolve():
        raise RuntimeError("Verifier-restart ready and continue markers must use different paths")
    if ready_file.exists() or continue_file.exists():
        raise RuntimeError("Remove stale verifier-restart ready/continue markers before running this phase")
    result["stack"] = assert_base_and_storage(current)
    configure_simulation("DELAYED_RESPONSE", delay_ms=6000, timeout_ms=8000)
    token = random_token()
    command, accepted = start_verified_capture(token, 1, timeout=args.timeout)
    accepted_history = status_history(command, {"PayloadAccepted_Status": {"OK"}}, timeout=args.timeout)
    marker = {"command_id": command["id"], "verification_token": token,
              "request_id": int(accepted["extra"]["requestId"]),
              "generation": int(accepted["extra"]["generation"]),
              "accepted_at": accepted["receptionTime"], "local_run_id": current["local_run_id"]}
    ready_file.parent.mkdir(parents=True, exist_ok=True)
    temporary = ready_file.with_name(ready_file.name + f".{secrets.token_hex(4)}.tmp")
    temporary.write_text(json.dumps(marker, indent=2) + "\n")
    temporary.replace(ready_file)
    result["restart_marker"] = str(ready_file)
    result["restart_target"] = marker
    result["accepted_history"] = accepted_history
    deadline = time.monotonic() + args.timeout
    while not continue_file.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    if not continue_file.exists():
        raise RuntimeError(f"Restart orchestrator did not create continue marker {continue_file}")
    result["continue_marker"] = str(continue_file)
    product = wait_capture_delivery(current, command, token, accepted=accepted,
                                    timeout=args.timeout, label="verifier process restart")
    accepted_time_before = accepted_history["stages"].get("PayloadAccepted_Time")
    accepted_time_after = product["history"]["stages"].get("PayloadAccepted_Time")
    if accepted_time_before is None or accepted_time_after is None:
        raise RuntimeError("PayloadAccepted_Time was not recorded before and after verifier restart")
    if accepted_time_before != accepted_time_after:
        raise RuntimeError(f"Verifier restart rewrote PayloadAccepted_Time: {accepted_time_before} -> {accepted_time_after}")
    result["accepted_time_persisted"] = {
        "before_restart": accepted_time_before,
        "after_restart": accepted_time_after,
    }
    configure_simulation("NORMAL", delay_ms=0, timeout_ms=8000)
    result.update({"result": "PASS", "capture": product})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("before-restart", "after-restart", "missing-receipt",
                                           "storage-fault", "verifier-restart"))
    parser.add_argument("--manifest", type=Path,
                        help="External JSON manifest shared across storage-root restarts")
    parser.add_argument("--corrupt-product", type=int,
                        help="Product ID whose stored bytes were corrupted by the test setup")
    parser.add_argument("--missing-product", type=int,
                        help="Product ID whose data file was removed by the test setup")
    parser.add_argument("--incomplete-product", type=int,
                        help="Product ID whose metadata record was made incomplete by the test setup")
    parser.add_argument("--expect-product", type=int, action="append", default=[],
                        help="Additional intact catalog product ID (repeat for seeded fixture entries)")
    parser.add_argument("--minimum-new-product-id", type=int,
                        help="In a non-full storage-fault phase, require the next accepted product ID to meet this floor")
    parser.add_argument("--full-catalog", action="store_true",
                        help="Also require a pre-seeded 16-product catalog and verify overflow rejection")
    parser.add_argument("--ready-file", type=Path,
                        help="Atomic marker written once verifier-restart has admitted its delayed command")
    parser.add_argument("--continue-file", type=Path,
                        help="External marker created after command_verifier has restarted")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    args = parser.parse_args()
    if args.timeout < 1:
        parser.error("--timeout must be a positive number of seconds")
    if args.phase in {"before-restart", "after-restart", "storage-fault"} and not args.manifest:
        parser.error(f"{args.phase} requires --manifest")
    if args.phase in {"missing-receipt", "verifier-restart"} and args.manifest:
        parser.error(f"{args.phase} does not use --manifest")
    if args.phase != "storage-fault" and any(getattr(args, name) is not None for name in
                                             ("corrupt_product", "missing_product", "incomplete_product")):
        parser.error("storage fault IDs are only used with storage-fault")
    if args.phase != "storage-fault" and args.expect_product:
        parser.error("--expect-product is only used with storage-fault")
    if args.phase != "storage-fault" and args.full_catalog:
        parser.error("--full-catalog is only used with storage-fault")
    if args.phase != "storage-fault" and args.minimum_new_product_id is not None:
        parser.error("--minimum-new-product-id is only used with storage-fault")
    if args.minimum_new_product_id is not None and args.minimum_new_product_id <= 0:
        parser.error("--minimum-new-product-id must be positive")
    if args.minimum_new_product_id is not None and args.full_catalog:
        parser.error("--minimum-new-product-id is only supported for non-full storage-fault phases")
    if args.phase != "verifier-restart" and args.ready_file:
        parser.error("--ready-file is only used with verifier-restart")
    if args.phase != "verifier-restart" and args.continue_file:
        parser.error("--continue-file is only used with verifier-restart")
    if args.phase == "verifier-restart" and (not args.ready_file or not args.continue_file):
        parser.error("verifier-restart requires both --ready-file and --continue-file")
    run_locked(args)


def run_locked(args):
    lock_path = ROOT / "yamcs" / "run" / "demo.lock"
    with lock_path.open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        execute(args)


def execute(args):
    current = current_run()
    path = evidence_path(current, args.phase)
    result = {"result": "IN_PROGRESS", "phase": args.phase,
              "hardware_or_rf_used": False, "direct_component_calls_used": False,
              "yamcs_instance": INSTANCE, "storage_root": current["storage_root"],
              "run_id": current["local_run_id"], "evidence": str(path)}
    try:
        if args.phase == "before-restart":
            before_restart(args, current, result)
        elif args.phase == "after-restart":
            after_restart(args, current, result)
        elif args.phase == "missing-receipt":
            missing_receipt(args, current, result)
        elif args.phase == "storage-fault":
            storage_fault(args, current, result)
        elif args.phase == "verifier-restart":
            verifier_restart(args, current, result)
        else:
            raise ValueError(f"Unsupported phase {args.phase}")
        result["result"] = "PASS"
    except Exception as exc:
        result["result"] = "FAIL"
        result["error"] = repr(exc)
        raise
    finally:
        write_evidence(path, result)


if __name__ == "__main__":
    main()
