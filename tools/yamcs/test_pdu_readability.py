#!/usr/bin/env python3
"""Local native F′ + real Yamcs + stateful PTY PDU acceptance. No hardware.

Only the pseudo-terminal allocated here is passed to F′. All operator commands,
events and telemetry go through the loopback Yamcs API. --hold keeps the local
GUI available briefly after PASS. The fake PDU ends with every output OFF.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import pty
import secrets
import select
import struct
import subprocess
import threading
import time
from urllib import error, parse

import smoke
from test_cdr_tcp_acceptance import ROOT, crc, free_port, terminate, wait_until

MANAGER = "ArtemisRpiTeensyDeployment.epsManager."
RAILS = (1, 2, 3, 4, 5, 7, 8, 9, 10)
CHANNELS = ("Rail3V3_1", "Rail3V3_2", "Rail5V_1", "Rail5V_2", "Rail5V_3",
            "Rail12V", "RailVBatt", "RailBurn1", "RailBurn2")


class StatefulPdu:
    def __init__(self):
        self.master, self.slave = pty.openpty()
        self.device = os.ttyname(self.slave)
        self.stop = threading.Event()
        self.requests, self.errors = [], []
        self.bitmap = 0
        self.drop = False
        self.next_fault = None
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def answer(self, opcode, payload):
        if opcode == 0:
            return b"\x02"
        if opcode == 1:
            return bytes([2, 1, 96, 9, 2, 2, 2])
        if opcode == 2:
            return struct.pack("<HBBIB", self.bitmap, 0, 0, 424242, 1)
        if opcode == 0x10 and payload == b"\xff":
            return bytes([255, 9] + [(self.bitmap >> i) & 1 for i in range(9)])
        if opcode in (0x10, 0x11) and payload[0] in RAILS:
            bit = RAILS.index(payload[0])
            if opcode == 0x11:
                self.bitmap = (self.bitmap & ~(1 << bit)) | (payload[1] << bit)
            return bytes([payload[0], (self.bitmap >> bit) & 1])
        raise ValueError(f"unexpected opcode/payload {opcode:02x}/{payload.hex()}")

    def run(self):
        pending = bytearray()
        while not self.stop.is_set():
            try:
                if not select.select([self.master], [], [], 0.1)[0]:
                    continue
                pending.extend(os.read(self.master, 4096))
                while len(pending) >= 7:
                    start = pending.find(b"\xd4\xc3")
                    if start < 0:
                        pending[:] = pending[-1:]
                        break
                    del pending[:start]
                    if len(pending) < 7:
                        break
                    size = int.from_bytes(pending[3:5], "little")
                    if size > 220:
                        raise ValueError("oversized UART packet")
                    if len(pending) < size + 7:
                        break
                    channel, local = pending[2], bytes(pending[5:5 + size])
                    expected = int.from_bytes(pending[5 + size:7 + size], "little")
                    del pending[:7 + size]
                    if channel != 2 or crc(local) != expected or len(local) < 13 or local[0] != 1:
                        raise ValueError("invalid UART/local RPC request")
                    frame = local[4:]
                    if frame[:3] != b"\xa5\x02\x00" or crc(frame[1:-2]) != int.from_bytes(frame[-2:], "little"):
                        raise ValueError("invalid PDU frame")
                    opcode, sequence, payload = frame[3], frame[4], frame[7:-2]
                    self.requests.append({"opcode": opcode, "payload": payload.hex(), "sequence": sequence})
                    if self.drop:
                        continue
                    answer = self.answer(opcode, payload)
                    if self.next_fault and self.next_fault[0] == opcode:
                        fault = self.next_fault[1]
                        self.next_fault = None
                        if fault == "malformed_all":
                            answer = answer[:-1] + b"\x02"
                        elif fault == "mismatch":
                            self.bitmap ^= 1 << RAILS.index(payload[0])
                            answer = bytes([payload[0], (self.bitmap >> RAILS.index(payload[0])) & 1])
                    response = bytes([0xA5, 2, 1, opcode, sequence, 0, len(answer)]) + answer
                    response += crc(response[1:]).to_bytes(2, "little")
                    reply = bytes([1, local[1], 0, len(response)]) + response
                    wrapped = b"\xd4\xc3\x02" + len(reply).to_bytes(2, "little") + reply + crc(reply).to_bytes(2, "little")
                    os.write(self.master, wrapped)
            except (OSError, ValueError, IndexError) as exc:
                if not self.stop.is_set():
                    self.errors.append(str(exc))
                return

    def close(self):
        self.stop.set()
        self.thread.join(2)
        os.close(self.master)
        os.close(self.slave)


def parameter(name):
    listing = smoke.api(f"/api/mdb/{smoke.INSTANCE}/parameters?" + parse.urlencode({"q": name, "limit": 500}))
    matches = [p["qualifiedName"] for p in listing.get("parameters", [])
               if p["qualifiedName"].endswith("/" + (MANAGER + name).replace(".", "/"))]
    if len(matches) != 1:
        raise RuntimeError(f"Parameter lookup {name}: {matches}")
    return smoke.api(f"/api/processors/{smoke.INSTANCE}/realtime/parameters/" + parse.quote(matches[0], safe="/"))


def state(name, expected):
    def acquired():
        value = parameter(name)
        return value if value.get("acquisitionStatus") == "ACQUIRED" and value.get("engValue", {}).get("stringValue") == expected else None
    return wait_until(acquired, 15, f"{name} did not become {expected}")


def command(name, args, event, predicate=lambda e: True, outcome="OK"):
    issued = smoke.issue(MANAGER + name, args)
    after = datetime.fromisoformat(issued["generationTime"].replace("Z", "+00:00"))
    observed = smoke.wait_for_event(event, after=after, predicate=predicate)
    opcode = next(a["value"]["uint32Value"] for a in issued["assignments"] if a["name"] == "OpCode")
    terminal = smoke.wait_for_event("OpCodeCompleted" if outcome == "OK" else "OpCodeError", after=after,
        predicate=lambda e: e.get("extra", {}).get("Opcode") == str(opcode) and
        (outcome == "OK" or e.get("extra", {}).get("error") == outcome))
    return {"command": issued, "event": observed, "terminal": terminal}


def all_rail_events(result, event_name, key):
    after = datetime.fromisoformat(result["command"]["generationTime"].replace("Z", "+00:00"))
    value = result["event"]["extra"][key]
    expected = {"RAIL_3V3_1", "RAIL_3V3_2", "RAIL_5V_1", "RAIL_5V_2", "RAIL_5V_3",
                "RAIL_12V", "RAIL_VBATT", "RAIL_BURN1", "RAIL_BURN2"}
    def acquired():
        events = [e for e in smoke.events(event_name) if e.get("extra", {}).get(key) == value
                  and datetime.fromisoformat(e["receptionTime"].replace("Z", "+00:00")) >= after]
        return events if {e.get("extra", {}).get("rail") for e in events} == expected and len(events) == 9 else None
    result["all_nine_events"] = wait_until(acquired, 10, "Missing/duplicate named rail events")


def compatibility(baseline, dictionary):
    before, after = json.loads(baseline.read_text()), json.loads(dictionary.read_text())
    counts = {}
    for category in ("commands", "events", "telemetryChannels"):
        current = {item["name"]: item for item in after[category]}
        for item in before[category]:
            if current.get(item["name"]) != item:
                raise RuntimeError(f"Existing dictionary entry changed: {category}/{item['name']}")
        counts[category] = len(before[category])
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--dictionary", required=True, type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--hold", type=int, default=0, help="seconds to keep local GUI open after PASS")
    args = parser.parse_args()
    run = ROOT / "yamcs/run" / ("pdu-readability-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + secrets.token_hex(3))
    run.mkdir(parents=True)
    result = {"result": "RUNNING", "scope": "native F′, loopback Yamcs, fake PTY PDU only; no hardware",
              "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              "dictionary_sha256": hashlib.sha256(args.dictionary.read_bytes()).hexdigest(), "checks": {}}
    if args.baseline:
        result["checks"]["legacy_dictionary_unchanged"] = compatibility(args.baseline, args.dictionary)
    pdu, app, ground, handles = StatefulPdu(), None, None, []
    try:
        port = free_port()
        env = dict(os.environ, N2_STORAGE_ROOT="onboard-storage", NEUTRON_PAYLOAD_SIM_ROOT=str(ROOT / "external/payload-neutron-simulation"))
        handles = [(run / name).open("wb") for name in ("app.log", "ground.log")]
        app = subprocess.Popen([str(args.binary.resolve()), "-a", "127.0.0.1", "-p", str(port), "-d", pdu.device],
                               cwd=run, env=env, stdout=handles[0], stderr=subprocess.STDOUT)
        wait_until(lambda: pdu.requests or app.poll() is not None, 15, "No fake PDU startup requests")
        if app.poll() is not None:
            raise RuntimeError("Native F′ exited; see app.log")
        ground = subprocess.Popen([str(ROOT / "tools/yamcs/run_cdr_tcp.sh"), "--dictionary", str(args.dictionary.resolve()),
                                   "--address", "127.0.0.1", "--port", str(port), "--run-dir", str(run / "ground")],
                                  cwd=ROOT, env=env, stdout=handles[1], stderr=subprocess.STDOUT)

        def ready():
            if app.poll() is not None or ground.poll() is not None:
                raise RuntimeError("F′/Yamcs exited during startup; see logs")
            try:
                return any(i["name"] == smoke.INSTANCE and i["state"] == "RUNNING" for i in smoke.api("/api/instances").get("instances", []))
            except (OSError, error.URLError):
                return False
        wait_until(ready, 100, "Yamcs startup timed out")
        checks = result["checks"]
        checks["startup_all_off"] = {name: state(name, "OFF") for name in CHANNELS}
        checks["info"] = command("GET_PDU_INFO", {}, "PduInfoResult", lambda e: e.get("extra", {}).get("major") == "2")
        checks["get_all"] = command("GET_ALL_PDU_RAILS", {}, "PduRailReadback")
        all_rail_events(checks["get_all"], "PduRailReadback", "requestId")
        set_args = {"rail": "RAIL_5V_1", "desiredState": "ON", "confirmation": "CONFIRM"}
        checks["set_on"] = command("SET_PDU_RAIL", set_args, "PduRailSetResult", lambda e: e.get("extra", {}).get("readback") == "ON")
        checks["on_telemetry"] = state("Rail5V_1", "ON")
        checks["single_get"] = command("GET_PDU_RAIL", {"rail": "RAIL_5V_1"}, "PduRailReadback", lambda e: e.get("extra", {}).get("readback") == "ON")
        checks["set_off"] = command("SET_PDU_RAIL", dict(set_args, desiredState="OFF"), "PduRailSetResult", lambda e: e.get("extra", {}).get("readback") == "OFF")
        checks["off_telemetry"] = state("Rail5V_1", "OFF")
        before = sum(r["opcode"] == 0x11 for r in pdu.requests)
        checks["cancel"] = command("SET_PDU_RAIL", dict(set_args, confirmation="CANCEL"), "PduCommandRejected", outcome="VALIDATION_ERROR")
        assert sum(r["opcode"] == 0x11 for r in pdu.requests) == before, "CANCEL transmitted a SET"
        checks["status"] = command("GET_PDU_STATUS", {}, "PduSummaryRail")
        all_rail_events(checks["status"], "PduSummaryRail", "sequence")
        pdu.next_fault = (0x10, "malformed_all")
        checks["malformed_all"] = command("GET_ALL_PDU_RAILS", {}, "PduOperationError", lambda e: e.get("extra", {}).get("reason") == "MALFORMED_PAYLOAD", outcome="EXECUTION_ERROR")
        pdu.next_fault = (0x11, "mismatch")
        checks["set_mismatch"] = command("SET_PDU_RAIL", set_args, "PduRailSetMismatch", lambda e: e.get("extra", {}).get("readback") == "OFF", outcome="EXECUTION_ERROR")
        checks["mismatch_actual_state"] = state("Rail5V_1", "OFF")
        pdu.drop = True
        checks["timeout"] = command("GET_PDU_RAIL", {"rail": "RAIL_5V_1"}, "PduOperationError", lambda e: e.get("extra", {}).get("reason") == "TIMEOUT", outcome="EXECUTION_ERROR")
        checks["timeout_stale"] = {name: state(name, "STALE") for name in CHANNELS}
        pdu.drop = False
        checks["recovered_all_off"] = {name: state(name, "OFF") for name in CHANNELS}
        assert pdu.bitmap == 0, "Fake PDU did not finish OFF"
        assert not pdu.errors, pdu.errors
        result["requests"] = pdu.requests
        result["result"] = "PASS"
        (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(f"PASS: {len(checks)} checks; fake rails all OFF. Evidence: {run / 'result.json'}", flush=True)
        if args.hold:
            print(f"Local GUI: {smoke.URL}; holding {args.hold}s", flush=True)
            deadline = time.monotonic() + args.hold
            while time.monotonic() < deadline:
                time.sleep(min(1, deadline - time.monotonic()))
    except BaseException as exc:
        if result["result"] != "PASS":
            result.update(result="FAIL", error=str(exc), requests=pdu.requests, fake_errors=pdu.errors)
            (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        raise
    finally:
        terminate(ground)
        terminate(app)
        pdu.close()
        for handle in handles:
            handle.close()


if __name__ == "__main__":
    main()
