#!/usr/bin/env python3
"""Check the N2 state-of-health (SOH) reports through the Yamcs HTTP API.

1. EMIT_SOH_SNAPSHOT: one Snapshot event and a fresh OverallHealth value.
2. SET_SOH_REPORTING ENABLED: SohReportingChanged, and a report at once (MVP step 2.1).
3. With --expect-periodic (builds that connect sohApp.run to rateGroup1): no report while
   reporting is off, then one every SOH_PERIOD_SECONDS while it is on, and none after
   SET_SOH_REPORTING DISABLED.
4. SET_SOH_REPORTING DISABLED: SohReportingChanged DISABLED.
5. SOH_PERIOD_SECONDS_PRM_SET 0: refused value is reported (SohParameterRejected), then 10 again.

Local stack (run.sh): no options needed. CDR TCP stack: pass --dictionary <its dictionary>.
Saves one JSON record. Local software evidence only: no board, RF or EPS hardware.
"""
import argparse
from datetime import datetime, timezone
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
SOH = DEPLOYMENT + ".sohApp"
PERIOD_SECONDS = 10  # SOH_PERIOD_SECONDS default


def api(path, body=None):
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


def issue(name, args):
    listing = api(f"/api/mdb/{INSTANCE}/commands?" + parse.urlencode({"q": name.split(".")[-1], "limit": 500}))
    matches = [c["qualifiedName"] for c in listing.get("commands", [])
               if c["qualifiedName"].endswith("/" + name.replace(".", "/"))]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one Yamcs command for {name}, found {matches}")
    return api(f"/api/processors/{INSTANCE}/{PROCESSOR}/commands/" + parse.quote(matches[0], safe="/"),
               {"args": args, "origin": "n2-local-yamcs-soh-check",
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


def events_after(query, event_name, released):
    return [e for e in archive_events(query) if e.get("type") == f"{SOH}.{event_name}"
            and parse_time(e["receptionTime"]) >= released]


def wait_for(query, predicate, what, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = [e for e in archive_events(query) if predicate(e)]
        if found:
            return found[-1]
        time.sleep(0.5)
    raise RuntimeError(f"No {what} in the Yamcs archive within {timeout}s")


def has_opcode(event, opcode):
    raw = str(event.get("extra", {}).get("Opcode", ""))
    readings = set()
    for base in (0, 16):
        try:
            readings.add(int(raw, base))
        except ValueError:
            pass
    return opcode in readings


def fresh_value(name, after, expected=None, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = parameter(name)
            generated = value.get("generationTime")
            text = value.get("engValue", {}).get("stringValue")
            if (value.get("acquisitionStatus") == "ACQUIRED" and generated and parse_time(generated) >= after
                    and (expected is None or text == expected)):
                return value
        except error.HTTPError as exc:
            if exc.code != 404:
                raise
        time.sleep(0.5)
    raise RuntimeError(f"No fresh value of {name}" + (f" equal to {expected}" if expected else ""))


def main():
    global URL, INSTANCE
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dictionary", help="matching F' JSON dictionary; required for the CDR TCP stack")
    parser.add_argument("--run-dir", help="folder for the JSON evidence")
    parser.add_argument("--expect-periodic", action="store_true",
                        help="also check timed reports (needs sohApp.run connected to rateGroup1)")
    parser.add_argument("--url", default=URL)
    parser.add_argument("--instance", default=INSTANCE)
    args = parser.parse_args()
    URL, INSTANCE = args.url.rstrip("/"), args.instance

    wait_for_instance()
    if args.dictionary:
        dictionary_path = Path(args.dictionary).expanduser().resolve()
        run_dir = (Path(args.run_dir).expanduser().resolve() if args.run_dir
                   else ROOT / "yamcs" / "run" / time.strftime("soh-check-%Y%m%d-%H%M%S"))
    else:
        stack = json.loads((ROOT / "yamcs" / "run" / "current.json").read_text())
        dictionary_path = Path(stack["dictionary"])
        run_dir = Path(args.run_dir).expanduser().resolve() if args.run_dir else Path(stack["run_dir"])
    dictionary = json.loads(dictionary_path.read_text())
    opcodes = {c["name"]: c["opcode"] for c in dictionary["commands"]}
    if SOH + ".SET_SOH_REPORTING" not in opcodes:
        raise RuntimeError(f"{SOH}.SET_SOH_REPORTING is not in {dictionary_path}. Rebuild, then restart the stack.")

    steps = []

    def step(label, command, cmd_args, checks):
        sent = issue(command, cmd_args)
        released = parse_time(sent["generationTime"])
        opcode = opcodes[command]
        record = {"step": label, "command": command.split(".")[-1], "args": cmd_args, "released": sent["generationTime"]}
        try:
            done = wait_for("Opcode", lambda e: e.get("type") == "CdhCore.cmdDisp.OpCodeCompleted"
                            and parse_time(e["receptionTime"]) >= released and has_opcode(e, opcode),
                            f"OpCodeCompleted for {command}")
            record["command_result"] = done.get("message")
            record["checks"] = [check(released) for check in checks]
            record["result"] = "PASS"
        except RuntimeError as failure:
            record["result"], record["failure"] = "FAIL", str(failure)
        steps.append(record)
        print(f"{record['result']:4}  {label}")
        for item in record.get("checks", []):
            print(f"        {item}")
        if record["result"] == "FAIL":
            print(f"        -> {record['failure']}")
        return released

    def event_check(name, query, **extra):
        def check(released):
            event = wait_for(query, lambda e: e.get("type") == f"{SOH}.{name}" and parse_time(e["receptionTime"]) >= released
                             and all(str(e.get("extra", {}).get(k)) == str(v) for k, v in extra.items()), name)
            return event.get("message")
        return check

    def value_check(name, expected=None):
        def check(released):
            value = fresh_value(f"{SOH}.{name}", released, expected)
            return f"{name} = {value.get('engValue', {}).get('stringValue')}"
        return check

    if args.expect_periodic:  # the timer is connected, but reporting is off at start-up: nothing may go down
        quiet_from = datetime.now(timezone.utc)
        time.sleep(PERIOD_SECONDS + 4)
        count = len(events_after("SOH overall", "Snapshot", quiet_from))
        steps.append({"step": "no reports while reporting is off", "result": "PASS" if count == 0 else "FAIL",
                      "snapshots_while_off": count})
        print(f"{'PASS' if count == 0 else 'FAIL':4}  no reports while reporting is off ({count} seen in {PERIOD_SECONDS + 4} s)")
    step("EMIT_SOH_SNAPSHOT: one report now", SOH + ".EMIT_SOH_SNAPSHOT", {},
         [event_check("Snapshot", "SOH overall"), value_check("OverallHealth")])
    enabled_at = step("SET_SOH_REPORTING ENABLED: report at once", SOH + ".SET_SOH_REPORTING", {"reporting": "ENABLED"},
                      [event_check("SohReportingChanged", "SOH reporting", reporting="ENABLED"),
                       event_check("Snapshot", "SOH overall"), value_check("SohReporting", "ENABLED")])
    if args.expect_periodic:
        time.sleep(2 * PERIOD_SECONDS + 4)
        count = len(events_after("SOH overall", "Snapshot", enabled_at))
        ok = 3 <= count <= 4  # the one at enable + two periodic, one more if a tick lands on the edge
        steps.append({"step": f"periodic reports every {PERIOD_SECONDS} s", "result": "PASS" if ok else "FAIL",
                      "snapshots_since_enable": count})
        print(f"{'PASS' if ok else 'FAIL':4}  periodic reports every {PERIOD_SECONDS} s ({count} reports in {2 * PERIOD_SECONDS + 4} s)")
    disabled_at = step("SET_SOH_REPORTING DISABLED", SOH + ".SET_SOH_REPORTING", {"reporting": "DISABLED"},
                       [event_check("SohReportingChanged", "SOH reporting", reporting="DISABLED"),
                        value_check("SohReporting", "DISABLED")])
    if args.expect_periodic:
        time.sleep(PERIOD_SECONDS + 4)
        count = len(events_after("SOH overall", "Snapshot", disabled_at))
        steps.append({"step": "no reports after DISABLED", "result": "PASS" if count == 0 else "FAIL",
                      "snapshots_after_disable": count})
        print(f"{'PASS' if count == 0 else 'FAIL':4}  no reports after DISABLED ({count} seen)")
    step("SOH_PERIOD_SECONDS = 0 is reported", SOH + ".SOH_PERIOD_SECONDS_PRM_SET", {"val": 0},
         [event_check("SohParameterRejected", "SOH parameter", value="0")])
    step("SOH_PERIOD_SECONDS back to 10", SOH + ".SOH_PERIOD_SECONDS_PRM_SET", {"val": PERIOD_SECONDS}, [])

    passed = sum(1 for s in steps if s["result"] == "PASS")
    verdict = "PASS" if passed == len(steps) else "FAIL"
    result = {"result": verdict, "passed": passed, "steps_total": len(steps),
              "periodic_checked": args.expect_periodic, "url": URL, "dictionary": str(dictionary_path),
              "dictionary_sha256": hashlib.sha256(dictionary_path.read_bytes()).hexdigest(), "steps": steps}
    run_dir.mkdir(parents=True, exist_ok=True)
    dest = run_dir / f"soh-check-{int(time.time() * 1000)}.json"
    dest.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{verdict}: {passed} of {len(steps)} steps")
    print(f"Evidence: {dest}")
    raise SystemExit(0 if verdict == "PASS" else 1)


if __name__ == "__main__":
    main()
