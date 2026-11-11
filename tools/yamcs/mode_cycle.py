#!/usr/bin/env python3
"""Walk the N2 demo modes through the Yamcs HTTP API and check every answer (roadmap D5).

Part A, nominal cycle: BASE -> scheduled collection -> Science Collection ->
Science Processing -> SET_MODE SCIENCE_DOWNLINK -> SET_MODE BASE, plus the
refusals an operator is most likely to hit on the way.
Part B, SAFE: ground SAFE in and out, commands refused inside SAFE, and, on a
local Yamcs build only, a simulated latching fault, the blocked exit,
CLEAR_SAFE_LATCH, the stability wait and the exit.
Part C, AUTO_RETURN_TO_BASE = 1 for one collection (MVP deck step 3.4: back to
BASE once the product is on the OBC), then back to 0.
Part D, SAFE capture control (SDD v4.0 Table 5): MissionApp tells PayloadManager to
inhibit and ScienceApp to cancel, both report back, then SAFE entry is complete. Direct
PayloadManager commands, ScienceApp requests and pass-through are all refused at the
one gate, a queued collection never fires, and after SAFE a capture works again.

Each step sends one command, waits for its CdhCore.cmdDisp result (OpCodeCompleted
or OpCodeError) and for every MissionApp, ScienceApp or PayloadManager event the step must produce. Saves one
JSON record. Local stack (run.sh): no options needed. CDR TCP stack
(run_cdr_tcp.sh): pass --dictionary <its matching JSON dictionary>.
Local software evidence only: no board, TCP, RF, payload or EPS hardware.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import time
from urllib import error, parse, request

ROOT = Path(__file__).resolve().parents[2]
URL = "http://127.0.0.1:8090"
INSTANCE = "fprime-project"
PROCESSOR = "realtime"
DEPLOYMENT = "ArtemisRpiTeensyDeployment"
MISSION = DEPLOYMENT + ".missionApp"
SCIENCE = DEPLOYMENT + ".scienceApp"
PAYLOAD = DEPLOYMENT + ".payloadManager"
# Short captures keep the cycle quick. CONFIGURE_CAPTURE_DURATION is volatile (lost on restart).
CAPTURE_SECONDS = 3
SCHEDULE_DELAY_SECONDS = 2
QUEUED_DELAY_SECONDS = 15  # long enough to reach SAFE first, short enough to wait out


def api(path, body=None):
    """GET (no body) or POST (JSON body) one Yamcs HTTP API path; return the JSON reply."""
    req = request.Request(URL + path, None if body is None else json.dumps(body).encode(),
                          {"Content-Type": "application/json"}, method="GET" if body is None else "POST")
    with request.urlopen(req, timeout=10) as response:
        return json.load(response)


def parse_time(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def wait_for_instance(timeout=90):
    deadline = time.monotonic() + timeout
    while True:
        try:
            if any(i["name"] == INSTANCE and i["state"] == "RUNNING"
                   for i in api("/api/instances").get("instances", [])):
                return
        except (error.URLError, TimeoutError, OSError):
            pass
        if time.monotonic() > deadline:
            raise RuntimeError(f"Yamcs instance did not reach RUNNING within {timeout}s")
        time.sleep(1)


def load_dictionary(dictionary_arg, run_dir_arg):
    """The dictionary (for opcodes) and the evidence folder, as report_mode.py finds them."""
    if dictionary_arg:
        dictionary_path = Path(dictionary_arg).expanduser().resolve()
        run_dir = (Path(run_dir_arg).expanduser().resolve() if run_dir_arg
                   else ROOT / "yamcs" / "run" / time.strftime("mode-cycle-%Y%m%d-%H%M%S"))
        stack = {"stack": "dictionary given with --dictionary"}
    else:
        stack = json.loads((ROOT / "yamcs" / "run" / "current.json").read_text())
        dictionary_path = Path(stack["dictionary"])
        run_dir = Path(run_dir_arg).expanduser().resolve() if run_dir_arg else Path(stack["run_dir"])
    dictionary = json.loads(dictionary_path.read_text())
    opcodes = {c["name"]: c["opcode"] for c in dictionary["commands"]}
    if MISSION + ".SET_MODE" not in opcodes:
        raise RuntimeError(f"{MISSION}.SET_MODE is not in {dictionary_path}. Rebuild, then restart the stack.")
    return stack, dictionary_path, run_dir, opcodes


def issue(name, args):
    listing = api(f"/api/mdb/{INSTANCE}/commands?" + parse.urlencode({"q": name.split(".")[-1], "limit": 500}))
    matches = [c["qualifiedName"] for c in listing.get("commands", [])
               if c["qualifiedName"].endswith("/" + name.replace(".", "/"))]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one Yamcs command for {name}, found {matches}")
    return api(f"/api/processors/{INSTANCE}/{PROCESSOR}/commands/" + parse.quote(matches[0], safe="/"),
               {"args": args, "origin": "n2-local-yamcs-mode-cycle",
                "sequenceNumber": int(time.time() * 1000) % 2147483647})


def parameter(name):
    listing = api(f"/api/mdb/{INSTANCE}/parameters?" + parse.urlencode({"q": name.split(".")[-1], "limit": 500}))
    matches = [p["qualifiedName"] for p in listing.get("parameters", [])
               if p["qualifiedName"].endswith("/" + name.replace(".", "/"))]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one Yamcs parameter for {name}, found {matches}")
    return api(f"/api/processors/{INSTANCE}/{PROCESSOR}/parameters/" + parse.quote(matches[0], safe="/"))


def archive_events(query):
    reply = api(f"/api/archive/{INSTANCE}/events?" + parse.urlencode({"q": query, "limit": 200, "order": "desc"}))
    return reply.get("events", reply.get("event", []))


def has_opcode(event, opcode):
    raw = str(event.get("extra", {}).get("Opcode", ""))
    readings = set()
    for base in (0, 16):
        try:
            readings.add(int(raw, base))
        except ValueError:
            pass
    return opcode in readings


class Expect:
    """One event a step must produce: its name, the text Yamcs can search for, and its arguments.

    The event comes from MissionApp unless `component` names another one (Part D).
    """

    def __init__(self, event, search, component=MISSION, **extra):
        self.event, self.search, self.component, self.extra = event, search, component, extra

    def matches(self, event, released):
        if event.get("type") != f"{self.component}.{self.event}":
            return False
        if parse_time(event["receptionTime"]) < released:
            return False
        args = event.get("extra", {})
        return all(str(args.get(key)) == str(value) for key, value in self.extra.items())

    def describe(self):
        args = " ".join(f"{k}={v}" for k, v in self.extra.items())
        return f"{self.event} {args}".strip()


def mode_changed(mode):
    return Expect("ModeChanged", "Mission mode changed to", mode=mode)


def plan(mode):
    return Expect("SubsystemStatesRequested", "asks EPS=", mode=mode)


def refused(requested, current, reason):
    return Expect("SetModeRejected", "SET_MODE", requested=requested, current=current, reason=reason)


def science(event, search, **extra):
    return Expect(event, search, component=SCIENCE, **extra)


def payload(event, search, **extra):
    return Expect(event, search, component=PAYLOAD, **extra)


def refused_at_gate():
    return payload("PayloadOperationStatus", "Payload operation", state="INHIBITED")


class Scenario:
    def __init__(self, opcodes):
        self.opcodes = opcodes
        self.steps = []

    def wait_for(self, query, predicate, what, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            found = [e for e in archive_events(query) if predicate(e)]
            if found:
                return found[-1]  # archive is newest first: take the earliest match
            time.sleep(0.5)
        raise RuntimeError(f"No {what} in the Yamcs archive within {timeout}s")

    def step(self, label, command, args, ok, expects=(), later=(), timeout=30):
        """Send one command and check its result plus every expected event.

        expects: events that must follow the command itself.
        later:   events that come after a wait (the scheduled collection), checked with a longer timeout.
        """
        opcode = self.opcodes[command]
        sent = issue(command, args)
        released = parse_time(sent["generationTime"])
        record = {"step": label, "command": command.split(".")[-1], "args": args,
                  "expected_result": "OK" if ok else "refused", "released": sent["generationTime"]}
        try:
            if ok:
                result = self.wait_for("Opcode", lambda e: e.get("type") == "CdhCore.cmdDisp.OpCodeCompleted"
                                       and parse_time(e["receptionTime"]) >= released and has_opcode(e, opcode),
                                       f"OpCodeCompleted for {command}", timeout)
            else:
                result = self.wait_for("Opcode", lambda e: e.get("type") == "CdhCore.cmdDisp.OpCodeError"
                                       and parse_time(e["receptionTime"]) >= released and has_opcode(e, opcode),
                                       f"OpCodeError for {command}", timeout)
            record["command_result"] = result.get("message")
            seen = []
            sequence = []
            for expect, wait in [(e, timeout) for e in expects] + [(e, 90) for e in later]:
                event = self.wait_for(expect.search, lambda e, x=expect: x.matches(e, released),
                                      expect.describe(), wait)
                seen.append(event.get("message"))
                sequence.append(event.get("seqNumber"))
            record["events"] = seen
            record["event_sequence"] = sequence
            record["result"] = "PASS"
        except RuntimeError as failure:
            record["result"] = "FAIL"
            record["failure"] = str(failure)
        self.steps.append(record)
        print(f"{record['result']:4}  {label}")
        for message in record.get("events", []):
            print(f"        {message}")
        if record["result"] == "FAIL":
            print(f"        -> {record['failure']}")
        return record["result"] == "PASS"

    def in_order(self, label, skip=0):
        """PASS when the last step's events, after the first `skip`, reached the ground in the listed order."""
        last = self.steps[-1]
        numbers = last.get("event_sequence", [])
        compared = numbers[skip:]
        ok = len(compared) > 1 and None not in compared and compared == sorted(compared)
        record = {"step": label, "released": last.get("released"), "event_sequence": numbers,
                  "result": "PASS" if ok else "FAIL"}
        self.steps.append(record)
        print(f"{record['result']:4}  {label}")
        return ok

    def check_parameter(self, label, name, expected, after, timeout=20):
        """A fresh telemetry sample of `name` equal to `expected`, generated after `after`."""
        record = {"step": label, "parameter": name, "expected": expected}
        deadline = time.monotonic() + timeout
        value = None
        while time.monotonic() < deadline:
            try:
                value = parameter(name)
                generated = value.get("generationTime")
                if (value.get("acquisitionStatus") == "ACQUIRED" and generated
                        and parse_time(generated) >= after
                        and value.get("engValue", {}).get("stringValue") == expected):
                    break
            except error.HTTPError as exc:
                if exc.code != 404:
                    raise
            time.sleep(0.5)
        else:
            value = None
        record["result"] = "PASS" if value else "FAIL"
        record["value"] = value
        self.steps.append(record)
        print(f"{record['result']:4}  {label}")
        return value is not None

    def none_since(self, label, after, unwanted, settle=3):
        """PASS when no unwanted event arrived after `after`. unwanted: Expect objects that must not match."""
        time.sleep(settle)  # let any late event reach the archive first
        record = {"step": label, "must_not_see": [u.describe() for u in unwanted]}
        found = [f"{u.describe()}: {e.get('message')}" for u in unwanted
                 for e in archive_events(u.search) if u.matches(e, after)]
        record["result"] = "FAIL" if found else "PASS"
        if found:
            record["failure"] = "; ".join(found)
        self.steps.append(record)
        print(f"{record['result']:4}  {label}")
        if found:
            print(f"        -> {record['failure']}")
        return not found


def latest_release(scenario):
    return parse_time(scenario.steps[-1]["released"])


def main():
    global URL, INSTANCE
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dictionary", help="matching F' JSON dictionary; required for the CDR TCP stack")
    parser.add_argument("--run-dir", help="folder for the JSON evidence")
    parser.add_argument("--url", default=URL, help=f"Yamcs HTTP base URL (default {URL})")
    parser.add_argument("--instance", default=INSTANCE, help=f"Yamcs instance (default {INSTANCE})")
    args = parser.parse_args()
    URL, INSTANCE = args.url.rstrip("/"), args.instance

    wait_for_instance()
    stack, dictionary_path, run_dir, opcodes = load_dictionary(args.dictionary, args.run_dir)
    s = Scenario(opcodes)
    M = MISSION

    print("== start")
    s.step("REPORT_MODE answers BASE on a fresh stack", M + ".REPORT_MODE", {}, True,
           [Expect("ModeReport", "Mission mode is", mode="BASE")])
    s.step("Shorten the capture for this run", SCIENCE + ".CONFIGURE_CAPTURE_DURATION",
           {"durationSeconds": CAPTURE_SECONDS}, True)

    print("== Part A: nominal cycle")
    s.step("SET_MODE SCIENCE_PROCESSING refused in BASE (automatic only)", M + ".SET_MODE",
           {"mode": "SCIENCE_PROCESSING"}, False, [refused("SCIENCE_PROCESSING", "BASE", "AUTOMATIC_ONLY")])
    s.step("SET_MODE SCIENCE_COLLECTION refused in BASE (use SCHEDULE_COLLECTION)", M + ".SET_MODE",
           {"mode": "SCIENCE_COLLECTION"}, False,
           [refused("SCIENCE_COLLECTION", "BASE", "USE_SCHEDULE_COLLECTION")])
    s.step("SCHEDULE_COLLECTION, then the payload drives Collection and Processing",
           M + ".SCHEDULE_COLLECTION", {"delaySeconds": SCHEDULE_DELAY_SECONDS}, True,
           [mode_changed("COLLECTION_PENDING")],
           later=[mode_changed("COLLECTING"), plan("SCIENCE_COLLECTION"),
                  mode_changed("SCIENCE_READY"), plan("SCIENCE_PROCESSING")])
    s.check_parameter("OperationsMode telemetry reads SCIENCE_PROCESSING", M + ".OperationsMode",
                      "SCIENCE_PROCESSING", latest_release(s))
    s.step("SET_MODE SCIENCE_DOWNLINK opens the downlink window", M + ".SET_MODE",
           {"mode": "SCIENCE_DOWNLINK"}, True, [mode_changed("DOWNLINKING"), plan("SCIENCE_DOWNLINK")])
    s.check_parameter("OperationsMode telemetry reads SCIENCE_DOWNLINK", M + ".OperationsMode",
                      "SCIENCE_DOWNLINK", latest_release(s))
    s.step("SET_MODE BASE closes the cycle", M + ".SET_MODE", {"mode": "BASE"}, True,
           [mode_changed("BASE"), plan("BASE")])
    s.step("SCHEDULE_COLLECTION 120 s (stays queued)", M + ".SCHEDULE_COLLECTION", {"delaySeconds": 120}, True,
           [mode_changed("COLLECTION_PENDING")])
    s.step("SET_MODE SCIENCE_DOWNLINK refused while a collection is queued", M + ".SET_MODE",
           {"mode": "SCIENCE_DOWNLINK"}, False,
           [refused("SCIENCE_DOWNLINK", "COLLECTION_PENDING", "COLLECTION_ACTIVE")])
    s.step("CANCEL_COLLECTION returns to BASE", M + ".CANCEL_COLLECTION", {}, True, [mode_changed("BASE")])

    print("== Part B: SAFE")
    s.step("SET_MODE SAFE from the ground", M + ".SET_MODE", {"mode": "SAFE"}, True,
           [mode_changed("SAFE"), plan("SAFE"),
            Expect("SafeModeEntered", "SAFE mode entered", cause="GROUND_COMMAND", latched="0")])
    s.check_parameter("OperationsMode telemetry reads SAFE", M + ".OperationsMode", "SAFE", latest_release(s))
    s.step("SCHEDULE_COLLECTION refused in SAFE", M + ".SCHEDULE_COLLECTION", {"delaySeconds": 5}, False,
           [Expect("MissionCommandRejected", "Mission command rejected", reason="2", value="5")])
    s.step("SET_MODE SCIENCE_DOWNLINK refused in SAFE", M + ".SET_MODE", {"mode": "SCIENCE_DOWNLINK"}, False,
           [refused("SCIENCE_DOWNLINK", "SAFE", "SAFE_LOCKED")])
    s.step("REPORT_MODE answers SAFE", M + ".REPORT_MODE", {}, True,
           [Expect("ModeReport", "Mission mode is", mode="SAFE")])
    s.step("SET_MODE BASE leaves a ground-commanded SAFE at once", M + ".SET_MODE", {"mode": "BASE"}, True,
           [mode_changed("BASE")])

    # Fault path. Local Yamcs builds accept SIMULATE_SAFE_FAULT. Other builds refuse it (reason 3).
    probe = issue(M + ".SIMULATE_SAFE_FAULT", {"cause": "LOW_POWER", "latch": 1})
    released = parse_time(probe["generationTime"])
    fault_ok = True
    try:
        s.wait_for("SAFE mode entered", lambda e: Expect("SafeModeEntered", "", cause="LOW_POWER",
                                                         latched="1").matches(e, released),
                   "SafeModeEntered LOW_POWER", 15)
    except RuntimeError:
        fault_ok = False
    if fault_ok:
        s.steps.append({"step": "SIMULATE_SAFE_FAULT LOW_POWER latch=1 enters SAFE", "result": "PASS",
                        "released": probe["generationTime"]})
        print("PASS  SIMULATE_SAFE_FAULT LOW_POWER latch=1 enters SAFE")
        s.step("SET_MODE BASE blocked by the latch", M + ".SET_MODE", {"mode": "BASE"}, False,
               [Expect("SafeExitBlocked", "SAFE exit blocked", latchedCause="LOW_POWER")])
        s.step("CLEAR_SAFE_LATCH", M + ".CLEAR_SAFE_LATCH", {}, True,
               [Expect("SafeLatchCleared", "SAFE latch cleared", cause="LOW_POWER")])
        s.step("SET_MODE BASE still blocked: fault too recent", M + ".SET_MODE", {"mode": "BASE"}, False,
               [Expect("SafeExitBlocked", "SAFE exit blocked", latchedCause="NONE")])
        time.sleep(11)  # past SAFE_EXIT_STABLE_SECONDS (default 10)
        s.step("SET_MODE BASE passes after the stability interval", M + ".SET_MODE", {"mode": "BASE"}, True,
               [mode_changed("BASE")])
    else:
        s.step("SIMULATE_SAFE_FAULT refused outside a local Yamcs build", M + ".SIMULATE_SAFE_FAULT",
               {"cause": "LOW_POWER", "latch": 1}, False,
               [Expect("MissionCommandRejected", "Mission command rejected", reason="3", value="0")])
    print("== Part C: AUTO_RETURN_TO_BASE (MVP deck step 3.4)")
    s.step("AUTO_RETURN_TO_BASE set to 1", M + ".AUTO_RETURN_TO_BASE_PRM_SET", {"val": 1}, True)
    s.step("SCHEDULE_COLLECTION: Collection, Processing, then BASE by itself", M + ".SCHEDULE_COLLECTION",
           {"delaySeconds": SCHEDULE_DELAY_SECONDS}, True, [mode_changed("COLLECTION_PENDING")],
           later=[mode_changed("COLLECTING"), mode_changed("SCIENCE_READY"), plan("SCIENCE_PROCESSING"),
                  mode_changed("BASE"), plan("BASE")])
    s.step("AUTO_RETURN_TO_BASE back to 0", M + ".AUTO_RETURN_TO_BASE_PRM_SET", {"val": 0}, True)

    print("== Part D: SAFE capture control, direct and queued")
    s.step("SET_MODE SAFE: PayloadManager inhibits, then SAFE entry completes", M + ".SET_MODE", {"mode": "SAFE"},
           True, [mode_changed("SAFE"), payload("CaptureControlApplied", "capture control", control="DISABLED"),
                  Expect("SafeModeEntered", "SAFE mode entered")])
    # ModeChanged stays out of this check. MissionApp sends the inhibit first and PayloadManager runs on its
    # own thread, so CaptureControlApplied can land either side of ModeChanged.
    s.in_order("The inhibit came before SAFE entry completed", skip=1)
    safe_from = latest_release(s)
    s.step("payloadManager.SCIENCE_CAPTURE refused at the gate", PAYLOAD + ".SCIENCE_CAPTURE",
           {"durationSeconds": CAPTURE_SECONDS}, False, [refused_at_gate()])
    s.step("payloadManager.START_PAYLOAD_COLLECTION refused at the gate", PAYLOAD + ".START_PAYLOAD_COLLECTION",
           {"collectionId": 77}, False, [refused_at_gate()])
    s.step("payloadManager.SYNTHETIC_PASSTHROUGH refused at the gate", PAYLOAD + ".SYNTHETIC_PASSTHROUGH",
           {"requestId": 77, "literalCommand": "SAFE-CHECK"}, False, [refused_at_gate()])
    s.step("scienceApp.SCIENCE_CAPTURE: queued, then refused at the gate", SCIENCE + ".SCIENCE_CAPTURE",
           {"durationSeconds": CAPTURE_SECONDS}, True,
           [science("ScienceOperationRejected", "rejected", state="INHIBITED")])
    s.none_since("No product, no COLLECTING and no SCIENCE_READY while in SAFE", safe_from,
                 [science("ScienceProductReady", "Science product ready"), mode_changed("COLLECTING"),
                  mode_changed("SCIENCE_READY")])
    s.step("REPORT_MODE still answers SAFE", M + ".REPORT_MODE", {}, True,
           [Expect("ModeReport", "Mission mode is", mode="SAFE")])
    s.step("SET_MODE BASE leaves SAFE and enables captures again", M + ".SET_MODE", {"mode": "BASE"}, True,
           [mode_changed("BASE"), payload("CaptureControlApplied", "capture control", control="ENABLED")])
    s.step(f"SCHEDULE_COLLECTION {QUEUED_DELAY_SECONDS} s queues a collection", M + ".SCHEDULE_COLLECTION",
           {"delaySeconds": QUEUED_DELAY_SECONDS}, True, [mode_changed("COLLECTION_PENDING")])
    s.step("SET_MODE SAFE cancels it", M + ".SET_MODE", {"mode": "SAFE"}, True,
           [mode_changed("SAFE"), science("CollectionCancelled", "cancelled"),
            Expect("SafeModeEntered", "SAFE mode entered")])
    s.in_order("The cancel came before SAFE entry completed")
    s.none_since("The queued collection never fires, not even to meet the gate", latest_release(s),
                 [science("ScienceProductReady", "Science product ready"), mode_changed("COLLECTING"),
                  science("ScienceOperationRejected", "rejected")],
                 settle=QUEUED_DELAY_SECONDS + 3)
    s.step("SET_MODE BASE leaves SAFE", M + ".SET_MODE", {"mode": "BASE"}, True, [mode_changed("BASE")])
    s.step("scienceApp.SCIENCE_CAPTURE after SAFE gives a stored product", SCIENCE + ".SCIENCE_CAPTURE",
           {"durationSeconds": CAPTURE_SECONDS}, True, [mode_changed("COLLECTING")],
           later=[science("ScienceProductReady", "Science product ready"), mode_changed("SCIENCE_READY")])
    s.step("SET_MODE BASE ends the cycle", M + ".SET_MODE", {"mode": "BASE"}, True, [mode_changed("BASE")])
    s.step("REPORT_MODE answers BASE at the end", M + ".REPORT_MODE", {}, True,
           [Expect("ModeReport", "Mission mode is", mode="BASE")])

    passed = sum(1 for step in s.steps if step["result"] == "PASS")
    verdict = "PASS" if passed == len(s.steps) else "FAIL"
    result = {"result": verdict, "passed": passed, "steps_total": len(s.steps),
              "fault_path": "simulated (local Yamcs build)" if fault_ok else "refused (not a local Yamcs build)",
              "url": URL, "local_run_id": stack.get("local_run_id"), "dictionary": str(dictionary_path),
              "dictionary_sha256": hashlib.sha256(dictionary_path.read_bytes()).hexdigest(), "steps": s.steps}
    run_dir.mkdir(parents=True, exist_ok=True)
    dest = run_dir / f"mode-cycle-{int(time.time() * 1000)}.json"
    dest.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{verdict}: {passed} of {len(s.steps)} steps")
    print(f"Evidence: {dest}")
    raise SystemExit(0 if verdict == "PASS" else 1)


if __name__ == "__main__":
    main()
