#!/usr/bin/env python3
"""Verify slice C through Yamcs commands and returned events/telemetry only."""
from datetime import datetime
import fcntl
import json
from pathlib import Path
import secrets
import time

from smoke import ROOT, issue, events, wait_for_event
from passthrough import DEPLOYMENT, parameter, wait_for_running
from demo import fresh_parameter

MANAGER = f"{DEPLOYMENT}.payloadManager"
TERMINAL = {"COMPLETED", "BUSY", "INVALID", "DRIVER_UNAVAILABLE", "FAILED", "TIMED_OUT"}
result = {"result": "IN_PROGRESS", "hardware_or_rf_used": False, "checks": []}
operations = []


def stamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def extra(event, name):
    return str(event.get("extra", {}).get(name))


def send(component, command, args=None):
    response = issue(f"{DEPLOYMENT}.{component}.{command}", args or {})
    result.setdefault("commands", []).append(response)
    return response


def returned(command, component, event_name, predicate=lambda e: True, timeout=15):
    return wait_for_event(event_name, after=stamp(command["generationTime"]), timeout=timeout,
                          predicate=lambda e: e.get("type") == f"{DEPLOYMENT}.{component}.{event_name}" and predicate(e))


def configure(fault="NORMAL", delay=0, timeout=8000):
    command = send("payloadManager", "CONFIGURE_SIMULATION", {
        "faultMode": fault, "delayMs": delay, "timeoutMs": timeout})
    event = returned(command, "payloadManager", "SimulationConfigured",
                     lambda e: extra(e, "faultMode") == fault and extra(e, "delayMs") == str(delay)
                     and extra(e, "timeoutMs") == str(timeout))
    result.setdefault("configurations", []).append(event)


def status(command, state, request_id=None, generation=None, kind=None, timeout=15):
    return returned(command, "payloadManager", "PayloadOperationStatus",
                    lambda e: extra(e, "state") == state
                    and (request_id is None or extra(e, "requestId") == str(request_id))
                    and (generation is None or extra(e, "generation") == str(generation))
                    and (kind is None or extra(e, "kind") == kind), timeout)


def literal(text="C lifecycle nominal", request_id=None, expected="COMPLETED", finish=True):
    request_id = request_id if request_id is not None else secrets.randbelow(0x7FFFFFFF) + 1
    command = send("payloadManager", "SYNTHETIC_PASSTHROUGH", {"requestId": request_id, "literalCommand": text})
    op = {"command": command, "requestId": request_id, "literal": text}
    if expected in {"BUSY", "INVALID", "DRIVER_UNAVAILABLE"}:
        op["terminal"] = status(command, expected, request_id, generation=0)
    else:
        op["accepted"] = status(command, "ACCEPTED", request_id)
        op["generation"] = int(extra(op["accepted"], "generation"))
        if finish:
            complete(op, expected)
    operations.append(op)
    return op


def complete(op, expected):
    op["terminal"] = status(op["command"], expected, op["requestId"], op["generation"])
    if expected == "COMPLETED" and "literal" in op:
        op["echo"] = returned(op["command"], "payloadManager", "SyntheticPassthroughResponse",
                             lambda e: extra(e, "requestId") == str(op["requestId"])
                             and e.get("extra", {}).get("literalEcho") == op["literal"])
    return op


def counts():
    command = send("storageManager", "REPORT_STORAGE_STATUS")
    released = stamp(command["generationTime"])
    # REPORT_STORAGE_STATUS writes StoredProducts even for an empty catalog.
    # The matching report event is StorageHistoryEmpty at zero, otherwise the
    # latest durable record's LatestDataset event.
    storage = fresh_parameter(f"{DEPLOYMENT}.storageManager.StoredProducts", released)
    stored_count = int(storage["engValue"]["uint32Value"])
    if stored_count == 0:
        report = returned(command, "storageManager", "StorageHistoryEmpty")
    else:
        report = returned(command, "storageManager", "LatestDataset",
                          lambda event: int(extra(event, "productCount")) == stored_count)
    # These values come from returned Yamcs telemetry, never from capture files.
    telemetry = fresh_parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount", started)
    result.setdefault("count_observations", []).append({"storage_report": report, "storage_telemetry": storage, "collection_telemetry": telemetry})
    value = telemetry["engValue"]
    return {"stored": stored_count,
            "collections": int(value["uint32Value"])}


def no_products(after, before):
    # Allow the local TM/event pipeline to settle, then prove the manager is
    # still ticking. An unchanged update-on-change counter keeps its old time.
    time.sleep(3)
    fresh_parameter(f"{MANAGER}.ManagerHeartbeat", after)
    after_counts = counts()
    if before != after_counts:
        raise RuntimeError(f"Failed/rejected capture changed product counts: {before} -> {after_counts}")
    forbidden = []
    for name in ("ScienceProductReady", "ScienceStored"):
        forbidden.extend(e for e in events(name) if stamp(e["receptionTime"]) >= after)
    forbidden.extend(e for e in events("ModeChanged") if stamp(e["receptionTime"]) >= after
                     and e.get("type") == f"{DEPLOYMENT}.missionApp.ModeChanged" and extra(e, "mode") == "SCIENCE_READY")
    if forbidden:
        raise RuntimeError(f"Failed/rejected capture published science products: {forbidden}")
    return after_counts


def capture(fault, expected, component="scienceApp", command_name="SCIENCE_CAPTURE", args=None):
    reset = send("missionApp", "ENTER_BASE_MODE")
    returned(reset, "missionApp", "ModeChanged", lambda e: extra(e, "mode") == "BASE")
    before = counts()
    configure(fault, timeout=1000 if expected == "TIMED_OUT" else 8000)
    command = send(component, command_name, args if args is not None else {"durationMinutes": 1})
    accepted = status(command, "ACCEPTED", kind="CAPTURE")
    op = {"command": command, "accepted": accepted, "counts_before": before,
          "requestId": int(extra(accepted, "requestId")), "generation": int(extra(accepted, "generation"))}
    operations.append(op)
    complete(op, expected)
    if expected != "COMPLETED":
        op["counts_unchanged"] = no_products(stamp(command["generationTime"]), before)
    else:
        op["ready"] = returned(command, "scienceApp", "ScienceProductReady")
        op["stored"] = returned(command, "storageManager", "ScienceStored")
        if int(extra(op["stored"], "productCount")) != before["stored"] + 1:
            raise RuntimeError("Successful capture did not store exactly one product")
        op["collection_count"] = fresh_parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount",
            stamp(command["generationTime"]), predicate=lambda v: int(v["engValue"]["uint32Value"]) == before["collections"] + 1)
    return op


def check(name, evidence):
    result["checks"].append({"name": name, "evidence": evidence})
    print(f"PASS: {name}", flush=True)


def current_status(command):
    return [e for e in events("PayloadOperationStatus") if e.get("type") == f"{MANAGER}.PayloadOperationStatus"
            and stamp(e["receptionTime"]) >= stamp(command["generationTime"])]


def probe():
    wait_for_running()
    configure()
    check("nominal correlated response", literal())
    check("empty literal rejected", literal("", expected="INVALID"))
    check("zero pass-through request ID rejected", literal("zero ID rejected", request_id=0, expected="INVALID"))
    command = send("payloadManager", "START_PAYLOAD_COLLECTION", {"collectionId": 0})
    check("zero explicit legacy collection ID rejected", status(command, "INVALID", request_id=0, generation=0, kind="CAPTURE"))
    command = send("payloadManager", "SCIENCE_CAPTURE", {"durationMinutes": 0})
    check("invalid duration rejected", status(command, "INVALID", kind="CAPTURE"))
    command = send("payloadManager", "CONFIGURE_SIMULATION", {"faultMode": "NORMAL", "delayMs": 0, "timeoutMs": 999})
    check("unsafe timeout configuration rejected", returned(command, "payloadManager", "SimulationConfigRejected"))
    configure("DRIVER_UNAVAILABLE")
    check("unavailable rejection", literal(expected="DRIVER_UNAVAILABLE"))
    check("recovery after unavailable", literal())

    before = counts()
    configure("DELAYED_RESPONSE", delay=4000)
    active = literal("busy owner", finish=False)
    same_id_command = send("payloadManager", "SYNTHETIC_PASSTHROUGH", {
        "requestId": active["requestId"], "literalCommand": "active ID contender"})
    contender_id = secrets.randbelow(0x7FFFFFFF) + 1
    literal_command = send("payloadManager", "SYNTHETIC_PASSTHROUGH", {"requestId": contender_id, "literalCommand": "busy contender"})
    busy_events = []
    contenders = []
    for component, command_name, args in (
        ("payloadManager", "START_PAYLOAD_COLLECTION", {"collectionId": 917}),
        ("payloadManager", "SCIENCE_CAPTURE", {"durationMinutes": 1}),
        ("scienceApp", "START_COLLECTION", {}),
        ("scienceApp", "SCIENCE_CAPTURE", {"durationMinutes": 1}),
        ("missionApp", "SCHEDULE_COLLECTION", {"delaySeconds": 1}),
    ):
        command = send(component, command_name, args)
        contenders.append(command)
    command = send("payloadManager", "CONFIGURE_SIMULATION", {"faultMode": "FAIL", "delayMs": 0, "timeoutMs": 1000})
    check("second pass-through busy", status(literal_command, "BUSY", contender_id))
    same_id_busy = status(same_id_command, "BUSY", active["requestId"], generation=0)
    for contender in contenders:
        busy_events.append(status(contender, "BUSY", kind="CAPTURE"))
    check("configuration while busy rejected", returned(command, "payloadManager", "SimulationConfigRejected"))
    complete(active, "COMPLETED")
    if stamp(same_id_busy["receptionTime"]) >= stamp(active["terminal"]["receptionTime"]):
        raise RuntimeError("Reused-ID contender was not rejected while its owner was pending")
    check("reuse of active request ID remains busy", same_id_busy)
    # Batched commands overlap in time, so require five distinct returned
    # events rather than letting one generic BUSY event satisfy five waits.
    busy_events = [e for e in current_status(contenders[0]) if extra(e, "state") == "BUSY"
                   and extra(e, "kind") == "CAPTURE"
                   and stamp(e["receptionTime"]) < stamp(active["terminal"]["receptionTime"])]
    if len({e["seqNumber"] for e in busy_events}) != len(contenders) or len(busy_events) != len(contenders):
        raise RuntimeError("Five capture contenders did not return five distinct BUSY events")
    if not any(extra(e, "requestId") == "917" for e in busy_events):
        raise RuntimeError("Explicit legacy collection ID was not correlated in BUSY response")
    check("all legacy capture entries share admission", {"commands": contenders, "busy_events": busy_events})
    if any(stamp(e["receptionTime"]) >= stamp(active["terminal"]["receptionTime"]) for e in busy_events):
        raise RuntimeError("A busy contender was not observed while owner was pending")
    check("busy capture produces no science", no_products(stamp(active["command"]["generationTime"]), before))
    collecting = [e for e in events("ModeChanged") if stamp(e["receptionTime"]) >= stamp(active["command"]["generationTime"])
                  and extra(e, "mode") == "COLLECTING"]
    if collecting:
        raise RuntimeError("Rejected capture falsely announced COLLECTING")

    configure("FAIL")
    check("explicit failure", literal(expected="FAILED"))
    check("recovery after failure", literal())
    configure("NO_RESPONSE", timeout=1000)
    check("no response times out", literal(expected="TIMED_OUT"))
    check("recovery after timeout", literal())

    request_id = secrets.randbelow(0x7FFFFFFF) + 1
    configure("DELAYED_RESPONSE", delay=6000, timeout=1000)
    old = literal("old generation", request_id, expected="TIMED_OUT")
    configure("DELAYED_RESPONSE", delay=4000, timeout=8000)
    new = literal("new generation", request_id, finish=False)
    ignored = returned(old["command"], "payloadManager", "PayloadReplyIgnored",
                       lambda e: extra(e, "requestId") == str(request_id) and extra(e, "generation") == str(old["generation"]))
    if new["generation"] == old["generation"]:
        raise RuntimeError("Reused request ID did not receive a new generation")
    premature = [e for e in current_status(new["command"]) if extra(e, "generation") == str(new["generation"])
                 and extra(e, "state") in TERMINAL]
    if premature:
        raise RuntimeError("Late old reply completed the newer pending operation")
    complete(new, "COMPLETED")
    if not (stamp(new["accepted"]["receptionTime"]) <= stamp(ignored["receptionTime"]) < stamp(new["terminal"]["receptionTime"])):
        raise RuntimeError("Late-reply test did not overlap the newer pending operation")
    check("late reply cannot complete reused request ID", {"old": old, "new": new, "ignored": ignored})

    configure("DUPLICATE_RESPONSE")
    duplicate = literal("duplicate disposition")
    duplicate["ignored"] = returned(duplicate["command"], "payloadManager", "PayloadReplyIgnored",
                                    lambda e: extra(e, "generation") == str(duplicate["generation"]))
    check("duplicate reply ignored", duplicate)

    check("failed capture does not publish science", capture("FAIL", "FAILED"))
    hung = capture("HANG_CAPTURE", "TIMED_OUT")
    hung["reaped"] = returned(hung["command"], "payloadDriverNeutronSim", "CaptureWorkerReaped",
                             lambda e: extra(e, "generation") == str(hung["generation"])
                             and extra(e, "exitStatus") == "124")
    hung["observed_reap_seconds"] = (stamp(hung["reaped"]["receptionTime"]) - stamp(hung["accepted"]["receptionTime"])).total_seconds()
    if hung["observed_reap_seconds"] > 8:
        raise RuntimeError("Hung capture exceeded the local 8-second observed recovery bound")
    check("hung child times out without false science", hung)
    # Allow the driver's bounded kill/reap to finish; prove availability via returned telemetry.
    recovered = fresh_parameter(f"{MANAGER}.PayloadDriverAvailable", stamp(hung["terminal"]["receptionTime"]),
                                timeout=10, predicate=lambda v: v.get("engValue", {}).get("stringValue") == "True")
    check("child slot recovered with fresh telemetry", recovered)
    token = secrets.randbelow(0x7FFFFFFF)
    command = send("missionApp", "PING", {"token": token})
    check("system responds after hung child", returned(command, "missionApp", "Pong", lambda e: extra(e, "token") == str(token)))
    check("fresh successful capture after hung child", capture("NORMAL", "COMPLETED"))
    reset = send("missionApp", "ENTER_BASE_MODE")
    returned(reset, "missionApp", "ModeChanged", lambda e: extra(e, "mode") == "BASE")
    before = counts()
    configure("DELAYED_RESPONSE", delay=4000, timeout=8000)
    command = send("scienceApp", "SCIENCE_CAPTURE", {"durationMinutes": 1})
    accepted = status(command, "ACCEPTED", kind="CAPTURE")
    owner = {"command": command, "accepted": accepted, "requestId": int(extra(accepted, "requestId")),
             "generation": int(extra(accepted, "generation"))}
    operations.append(owner)
    check("capture owner rejects pass-through contender", literal("capture owns admission", expected="BUSY"))
    contender = send("scienceApp", "START_COLLECTION")
    rejection = status(contender, "BUSY", kind="CAPTURE")
    mode = fresh_parameter(f"{DEPLOYMENT}.missionApp.CurrentMode", stamp(command["generationTime"]),
                           predicate=lambda v: v.get("engValue", {}).get("stringValue") == "COLLECTING")
    complete(owner, "COMPLETED")
    owner["stored"] = returned(command, "storageManager", "ScienceStored",
                              lambda e: int(extra(e, "productCount")) == before["stored"] + 1)
    owner["collection_count"] = fresh_parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount", stamp(command["generationTime"]),
        predicate=lambda v: int(v["engValue"]["uint32Value"]) == before["collections"] + 1)
    check("capture busy rejection preserves accepted owner", {"owner": owner, "busy": rejection, "collecting": mode})
    duplicated_capture = capture("DUPLICATE_RESPONSE", "COMPLETED")
    duplicated_capture["ignored"] = returned(duplicated_capture["command"], "payloadManager", "PayloadReplyIgnored",
                                            lambda e: extra(e, "generation") == str(duplicated_capture["generation"]))
    time.sleep(3)
    expected_counts = {key: value + 1 for key, value in duplicated_capture["counts_before"].items()}
    duplicated_capture["counts_after"] = counts()
    if duplicated_capture["counts_after"] != expected_counts:
        raise RuntimeError("Duplicate capture reply changed completion/storage more than once")
    check("duplicate capture publishes exactly once", duplicated_capture)
    invalid = send("payloadManager", "SCIENCE_CAPTURE", {"durationMinutes": 0})
    check("invalid duration remains rejected after captures", status(invalid, "INVALID", kind="CAPTURE"))
    legacy = capture("NORMAL", "COMPLETED", "payloadManager", "START_PAYLOAD_COLLECTION", {"collectionId": 909})
    if legacy["requestId"] != 909:
        raise RuntimeError("Legacy collection ID was not preserved as request identity")
    legacy["duration"] = fresh_parameter(f"{DEPLOYMENT}.payloadDriverNeutronSim.LastDurationMinutes", stamp(legacy["command"]["generationTime"]),
                                        predicate=lambda v: int(v["engValue"]["uint32Value"]) == 1)
    check("invalid duration does not poison legacy capture configuration", legacy)
    check("scheduled capture uses same accepted lifecycle", capture("NORMAL", "COMPLETED", "missionApp", "SCHEDULE_COLLECTION", {"delaySeconds": 1}))
    configure()
    send("missionApp", "ENTER_BASE_MODE")
    time.sleep(1)
    for op in operations:
        if "generation" not in op:
            continue
        matching = [e for e in current_status(op["command"]) if extra(e, "generation") == str(op["generation"]) and extra(e, "state") in TERMINAL]
        if len(matching) != 1:
            raise RuntimeError(f"Generation {op['generation']} has {len(matching)} terminal events")
    check("exactly one terminal event per admitted generation", [op["generation"] for op in operations if "generation" in op])
    result["result"] = "PASS"


if __name__ == "__main__":
    current = json.loads((ROOT / "yamcs/run/current.json").read_text())
    started = stamp(current["started_at"])
    result["run_manifest"] = current
    path = Path(current["run_dir"]) / f"lifecycle-{secrets.token_hex(6)}.json"
    with (ROOT / "yamcs/run/demo.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            probe()
        except Exception as exc:
            result.update(result="FAIL", error=str(exc))
            raise
        finally:
            result["operations"] = operations
            path.write_text(json.dumps(result, indent=2) + "\n")
            print(f"{result['result']}: {len(result['checks'])} checks; evidence: {path}")
