#!/usr/bin/env python3
"""Ask the N2 flight software for its mission mode, exclusively through the Yamcs HTTP API.

Sends missionApp.REPORT_MODE the way an operator would, then checks Yamcs's own
event archive for:
  1. exactly one missionApp.ModeReport answer (C&T Modes rows 2 and 3), and
  2. the stock CdhCore.cmdDisp OpCodeDispatched + OpCodeCompleted events for its
     opcode (the PUS 1 style acceptance and completion reports).
Saves a JSON record next to the run's other evidence.

Local stack (run.sh): no options needed; the dictionary and run folder come from
yamcs/run/current.json. CDR TCP stack (run_cdr_tcp.sh): that launcher does not write
current.json, so pass --dictionary <its matching JSON dictionary>.
Local software evidence only: it does not prove any board, TCP, or RF path.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import time
from urllib import error, parse, request

ROOT = Path(__file__).resolve().parents[2]
# Defaults for the two Yamcs settings; main() replaces them with --url / --instance.
URL = "http://127.0.0.1:8090"
INSTANCE = "fprime-project"
PROCESSOR = "realtime"
COMMAND = "ArtemisRpiTeensyDeployment.missionApp.REPORT_MODE"
REPORT_EVENT = "ArtemisRpiTeensyDeployment.missionApp.ModeReport"
DISPATCHED_EVENT = "CdhCore.cmdDisp.OpCodeDispatched"
COMPLETED_EVENT = "CdhCore.cmdDisp.OpCodeCompleted"
# Components.MissionMode in Types.fpp, in value order (BASE = 0 ... DOWNLINKING = 4).
MODES = ("BASE", "COLLECTION_PENDING", "COLLECTING", "SCIENCE_READY", "DOWNLINKING")
# After completion, wait this long before counting answers, so a duplicate would be caught.
SETTLE_SECONDS = 3


def api(path, body=None):
    """GET (no body) or POST (JSON body) one Yamcs HTTP API path; return the JSON reply."""
    req = request.Request(URL + path, None if body is None else json.dumps(body).encode(),
                          {"Content-Type": "application/json"}, method="GET" if body is None else "POST")
    with request.urlopen(req, timeout=10) as response:
        return json.load(response)


def parse_time(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def wait_for_instance(timeout=90):
    """Block until the Yamcs instance reports RUNNING."""
    deadline = time.monotonic() + timeout
    while True:
        try:
            instances = api("/api/instances")
            if any(i["name"] == INSTANCE and i["state"] == "RUNNING" for i in instances.get("instances", [])):
                return
        except (error.URLError, TimeoutError, OSError):
            pass
        if time.monotonic() > deadline:
            raise RuntimeError(f"Yamcs instance did not reach RUNNING within {timeout}s")
        time.sleep(1)


def stack_and_opcode(dictionary_arg, run_dir_arg):
    """Find REPORT_MODE's opcode and the folder for the evidence.

    With --dictionary (CDR TCP stack): use that dictionary, and --run-dir or a new
    yamcs/run/report-mode-<time>/ folder. Without it (local stack): use the dictionary
    and run folder that run.sh recorded in yamcs/run/current.json.
    """
    if dictionary_arg:
        stack = {"stack": "dictionary given with --dictionary"}
        dictionary_path = Path(dictionary_arg).expanduser().resolve()
        run_dir = (Path(run_dir_arg).expanduser().resolve() if run_dir_arg
                   else ROOT / "yamcs" / "run" / time.strftime("report-mode-%Y%m%d-%H%M%S"))
    else:
        stack = json.loads((ROOT / "yamcs" / "run" / "current.json").read_text())
        dictionary_path = Path(stack["dictionary"])
        run_dir = Path(run_dir_arg).expanduser().resolve() if run_dir_arg else Path(stack["run_dir"])
    dictionary = json.loads(dictionary_path.read_text())
    for command in dictionary["commands"]:
        if command["name"] == COMMAND:
            return stack, dictionary_path, run_dir, command["opcode"]
    raise RuntimeError(f"{COMMAND} is not in {dictionary_path}. Rebuild (tools/yamcs/build_native.sh or "
                       "build_cdr_tcp.sh), then restart the stack.")


def issue_report_mode():
    """Send REPORT_MODE (no arguments) through Yamcs, resolving its name from Yamcs's MDB."""
    listing = api(f"/api/mdb/{INSTANCE}/commands?" + parse.urlencode({"q": "REPORT_MODE", "limit": 500}))
    matches = [c["qualifiedName"] for c in listing.get("commands", [])
               if c["qualifiedName"].endswith("/" + COMMAND.replace(".", "/"))]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one Yamcs command for {COMMAND}, found {matches}")
    return api(f"/api/processors/{INSTANCE}/{PROCESSOR}/commands/" + parse.quote(matches[0], safe="/"),
               {"args": {}, "origin": "n2-local-yamcs-report-mode",
                "sequenceNumber": int(time.time()) % 2147483647})


def archive_events(query):
    """Newest-first archive search, so events from earlier runs never crowd out this one."""
    reply = api(f"/api/archive/{INSTANCE}/events?" + parse.urlencode({"q": query, "limit": 100, "order": "desc"}))
    return reply.get("events", reply.get("event", []))


def wait_for_event(query, predicate, what, timeout=30):
    """Poll the archive until an event matches; return every match found at that moment."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = [e for e in archive_events(query) if predicate(e)]
        if found:
            return found
        time.sleep(0.5)
    raise RuntimeError(f"No {what} in the Yamcs archive within {timeout}s")


def has_opcode(event, opcode):
    """cmdDisp events carry the opcode in their 'Opcode' argument, as decimal or hex text."""
    raw = str(event.get("extra", {}).get("Opcode", ""))
    readings = set()
    for base in (0, 16):
        try:
            readings.add(int(raw, base))
        except ValueError:
            pass
    return opcode in readings


def mode_name(value):
    """The ModeReport 'mode' argument as a MissionMode name, whether Yamcs gives a name or a number."""
    text = str(value)
    return MODES[int(text)] if text.isdigit() and int(text) < len(MODES) else text


def main():
    global URL, INSTANCE
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--expect-mode", choices=MODES,
                        help="also require this mode in the answer (a fresh stack starts in BASE)")
    parser.add_argument("--dictionary",
                        help="matching F' JSON dictionary; required for the CDR TCP stack (run_cdr_tcp.sh)")
    parser.add_argument("--run-dir", help="folder for the JSON evidence (default: the local stack's run folder, "
                        "or yamcs/run/report-mode-<time>/ with --dictionary)")
    parser.add_argument("--url", default=URL, help=f"Yamcs HTTP base URL (default {URL})")
    parser.add_argument("--instance", default=INSTANCE, help=f"Yamcs instance (default {INSTANCE})")
    args = parser.parse_args()
    URL, INSTANCE = args.url.rstrip("/"), args.instance  # every Yamcs call below uses these two

    wait_for_instance()
    stack, dictionary_path, run_dir, opcode = stack_and_opcode(args.dictionary, args.run_dir)
    opcode_text = f"0x{opcode:x}"  # cmdDisp messages print opcodes as 0x{x}

    command = issue_report_mode()
    released = parse_time(command["generationTime"])

    def after_release(event):
        return parse_time(event["receptionTime"]) >= released

    def is_report(event):
        return event.get("type") == REPORT_EVENT and after_release(event)

    def is_cmd_disp(event_type):
        return lambda e: e.get("type") == event_type and after_release(e) and has_opcode(e, opcode)

    # 1. The answer from MissionApp.
    wait_for_event("Mission mode is", is_report, "missionApp.ModeReport")
    # 2. Acceptance and completion from the stock F' command dispatcher.
    dispatched = wait_for_event("Opcode", is_cmd_disp(DISPATCHED_EVENT), f"OpCodeDispatched for {opcode_text}")
    completed = wait_for_event("Opcode", is_cmd_disp(COMPLETED_EVENT),
                               f"OpCodeCompleted for {opcode_text} (look for CdhCore.cmdDisp.OpCodeError)")
    # 3. Exactly one answer: give a duplicate time to arrive, then count again.
    time.sleep(SETTLE_SECONDS)
    reports = [e for e in archive_events("Mission mode is") if is_report(e)]
    if len(reports) != 1:
        raise RuntimeError(f"Expected exactly one ModeReport after the command, found {len(reports)}")
    mode = mode_name(reports[0].get("extra", {}).get("mode"))
    if mode not in MODES:
        raise RuntimeError(f"ModeReport named an unknown mode: {mode!r}")
    if args.expect_mode and mode != args.expect_mode:
        raise RuntimeError(f"Expected mode {args.expect_mode}, MissionApp answered {mode}")

    result = {"result": "PASS", "mode": mode, "opcode": opcode_text, "url": URL,
              "local_run_id": stack.get("local_run_id"), "dictionary": str(dictionary_path),
              "dictionary_sha256": hashlib.sha256(dictionary_path.read_bytes()).hexdigest(),
              "command": command, "mode_report_from_yamcs": reports[0],
              "dispatched_from_yamcs": dispatched[0], "completed_from_yamcs": completed[0]}
    run_dir.mkdir(parents=True, exist_ok=True)
    dest = run_dir / f"report-mode-{int(time.time() * 1000)}.json"
    dest.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    print(f"Mission mode is {mode}")
    print(f"Evidence: {dest}")


if __name__ == "__main__":
    main()
