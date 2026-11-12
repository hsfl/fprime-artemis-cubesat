#!/usr/bin/env python3
"""Yamcs-only mission commands and independently verified C3M twin receipts."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import time
from urllib import error, parse, request

from common import crc16

ROOT = Path(__file__).resolve().parents[2]
INSTANCE = "fprime-project"
PROCESSOR = "realtime"


class Probe:
    def __init__(self, url: str, devices: str, timeout: int = 180):
        self.url, self.devices = url.rstrip("/"), devices.rstrip("/")
        self.receipts = {}
        self.timeout = timeout
        self.names = {}

    @staticmethod
    def fetch(url, body=None, raw=False):
        data = None if body is None else json.dumps(body).encode()
        req = request.Request(url, data, {"Content-Type": "application/json"},
                              method="GET" if body is None else "POST")
        with request.urlopen(req, timeout=10) as response:
            payload = response.read()
        return payload if raw else json.loads(payload)

    def api(self, path, body=None, raw=False):
        return self.fetch(self.url + path, body, raw)

    def resolve(self, kind, suffix):
        key = kind, suffix
        if key not in self.names:
            result = self.api(f"/api/mdb/{INSTANCE}/{kind}?" + parse.urlencode({"q": suffix.split(".")[-1], "limit": 1000}))
            matches = [entry["qualifiedName"] for entry in result.get(kind, [])
                       if entry["qualifiedName"].replace("|", "/").endswith("/" + suffix.replace(".", "/"))]
            if len(matches) != 1:
                raise RuntimeError(f"Expected one {kind} entry ending in {suffix}: {matches}")
            self.names[key] = matches[0]
        return self.names[key]

    def issue(self, suffix, arguments=None):
        name = self.resolve("commands", suffix)
        event_path = f"/api/archive/{INSTANCE}/events?" + parse.urlencode({"limit": 200, "order": "desc"})
        def identity(event):
            return event.get("generationTime"), event.get("source"), event.get("seqNumber")
        previous_events = {identity(event) for event in self.api(event_path).get("events", [])}
        submission = self.api(f"/api/processors/{INSTANCE}/{PROCESSOR}/commands/" + parse.quote(name, safe="/"),
                              {"args": arguments or {}, "origin": "artemis-local-twin-demo",
                               "sequenceNumber": time.time_ns() % 2147483647})
        # The pinned adapter does not implement Yamcs CommandComplete. Check the
        # actual F Prime dispatcher event, with one outstanding harness command.
        current = json.loads((ROOT / ".twin-runs/current.json").read_text())
        dictionary = json.loads((Path(current["run_dir"]) / "ground/dictionaries/fc.json").read_text())
        commands = [entry for entry in dictionary["commands"] if entry["name"].endswith("." + suffix)]
        if len(commands) != 1:
            raise RuntimeError(f"Cannot resolve actual F Prime opcode for {suffix}")
        opcode = commands[0]["opcode"]
        opcode = int(opcode, 0) if isinstance(opcode, str) else opcode
        def completed():
            events = self.api(event_path)
            for event in events.get("events", []):
                if event.get("source") != "FPrimeEventProcessor" or identity(event) in previous_events:
                    continue
                extra = event.get("extra", {})
                try:
                    event_opcode = int(extra.get("Opcode", "-1"), 0)
                except (TypeError, ValueError):
                    continue
                if event_opcode != opcode:
                    continue
                kind = event.get("type", "")
                if kind.endswith(".OpCodeError") or kind.endswith(".InvalidCommand") or kind.endswith(".TooManyCommands"):
                    raise RuntimeError(f"F Prime rejected {suffix}: {event}")
                if kind.endswith(".OpCodeCompleted"):
                    return event
            return None
        completion = self.wait(f"actual F Prime completion of {suffix}", completed, 20)
        return {"submission": submission, "fprime_completion": completion,
                "completion_scope": "serialized command opcode/event; pinned adapter has no Yamcs CommandComplete verifier"}

    @staticmethod
    def value(parameter):
        raw = parameter.get("engValue", {})
        for key in ("stringValue", "uint32Value", "sint32Value", "uint64Value", "booleanValue"):
            if key in raw:
                value = raw[key]
                if isinstance(value, str) and value.lower() in ("true", "false"):
                    return value.lower() == "true"
                return value
        return None

    def parameter(self, suffix):
        name = self.resolve("parameters", suffix)
        return self.api(f"/api/processors/{INSTANCE}/{PROCESSOR}/parameters/" + parse.quote(name, safe="/"))

    def wait(self, description, predicate, timeout=None):
        deadline = time.monotonic() + (timeout or self.timeout)
        last = None
        while time.monotonic() < deadline:
            try:
                result = predicate()
                if result:
                    return result
            except (error.HTTPError, error.URLError, OSError, ValueError) as exc:
                last = str(exc)
            time.sleep(0.25)
        raise RuntimeError(f"Timed out waiting for {description}; last error: {last}")

    def wait_value(self, suffix, acceptable, timeout=None):
        def match():
            result = self.parameter(suffix)
            return result if self.value(result) in acceptable else None
        return self.wait(suffix, match, timeout)

    def receipt_list(self):
        try:
            result = self.api("/api/storage/buckets/artemis-science/objects?limit=1000")
            run_id = json.loads((ROOT / ".twin-runs/current.json").read_text())["run_id"]
            for item in result.get("objects", []):
                name = item["name"]
                if item.get("metadata", {}).get("runId") != run_id or not name.endswith(".fdp.receipt.json") or name in self.receipts:
                    continue
                self.receipts[name] = self.api("/api/storage/buckets/artemis-science/objects/" + parse.quote(name, safe=""))
            return list(self.receipts.values())
        except error.HTTPError as exc:
            if exc.code == 404:
                return []
            raise

    def fault(self, **changes):
        return self.fetch(self.devices + "/faults", changes)


def run(captures: int, pi_off_repair: bool, timeout: int) -> dict:
    current = json.loads((ROOT / ".twin-runs/current.json").read_text())
    run_directory = Path(current["run_dir"])
    probe = Probe("http://127.0.0.1:8090", "http://127.0.0.1:8092", timeout)
    result = {"result": "IN_PROGRESS", "run_id": current["run_id"], "cycles": [],
              "command_route": "Yamcs → FC mission authority", "science_route": "Pi → full FC cache → radio → ground → Yamcs GET"}
    destination = run_directory / ("acceptance-pi-off-repair.json" if pi_off_repair else "acceptance.json")
    try:
        probe.wait("Yamcs RUNNING", lambda: any(instance["name"] == INSTANCE and instance["state"] == "RUNNING"
                                               for instance in probe.api("/api/instances").get("instances", [])), 90)
        for cycle in range(captures):
            record = {"cycle": cycle + 1}
            result["cycles"].append(record)
            record["enter_base"] = probe.issue("missionApp.ENTER_BASE_MODE")
            record["base"] = probe.wait_value("missionApp.CurrentMode", {"BASE", 2})
            record["payload_ready"] = probe.wait_value("pcLinkManager.PayloadState", {"READY", 2}, 60)
            record["ping"] = probe.issue("missionApp.PING")
            record["soh"] = probe.issue("missionApp.EMIT_SOH_SNAPSHOT")
            record["duration"] = probe.issue("scienceCoordinator.CONFIGURE_CAPTURE_DURATION", {"durationSeconds": 1})
            old_objects = {receipt["object"] for receipt in probe.receipt_list()}
            record["schedule"] = probe.issue("missionApp.SCHEDULE_COLLECTION", {"delaySeconds": 2})
            record["started"] = probe.wait_value("scienceCoordinator.JobState", {"SCHEDULED", "CAPTURING", "STAGING", 1, 2, 3})
            record["cache_ready"] = probe.wait_value("scienceCoordinator.JobState", {"CACHE_READY", 4})
            record["cache_valid"] = probe.wait_value("scienceCoordinator.CacheValid", {True, "true", 1})
            record["product_id"] = int(probe.value(probe.parameter("scienceCoordinator.ProductId")))
            record["product_bytes"] = int(probe.value(probe.parameter("scienceCoordinator.ProductBytes")))
            if pi_off_repair and cycle == 0:
                record["fault"] = probe.fault(drop_science_packet_once=0)
                record["pi_off_command"] = probe.issue("missionApp.ENTER_STANDBY_MODE")
                probe.wait_value("missionApp.CurrentMode", {"STANDBY", 0})
                record["pi_off"] = probe.wait("virtual Pi power off", lambda: (state if not (state := probe.fetch(probe.devices + "/state"))["power"] else None))
            record["downlink"] = probe.issue("missionApp.REQUEST_SCIENCE_DOWNLINK")
            def received():
                matches = [receipt for receipt in probe.receipt_list()
                           if receipt["object"] not in old_objects and receipt["product_id"] == record["product_id"]
                           and receipt["verified_ground_receipt"] is True and receipt["run_id"] == current["run_id"]]
                return matches[-1] if matches else None
            receipt = record["receipt"] = probe.wait("new verified science archive receipt", received)
            raw = probe.api("/api/storage/buckets/artemis-science/objects/" + parse.quote(receipt["object"], safe=""), raw=True)
            if len(raw) != record["product_bytes"] or hashlib.sha256(raw).hexdigest() != receipt["sha256"] or crc16(raw) != receipt["crc16"]:
                raise RuntimeError("Independent Yamcs GET size/SHA differs from FC and receipt")
            # A read-only oracle after delivery. It cannot transport source bytes to ground.
            command = ["docker", "compose", "-f", str(ROOT / "twin/compose.yaml"), "exec", "-T", "payload-computer",
                       "python", "tools/twin/source_oracle.py"]
            source = json.loads(subprocess.check_output(command, cwd=ROOT, text=True, timeout=15))
            if source["sha256"] != receipt["sha256"] or source["bytes"] != len(raw):
                raise RuntimeError("Persisted Pi source differs from independently downlinked/archived bytes")
            record["source_oracle"] = source
            if pi_off_repair and cycle == 0:
                if receipt["retry_rounds"] < 1:
                    raise RuntimeError("Pi-off proof did not exercise packet repair")
                probe.fault(drop_science_packet_once=-1)
            record["result"] = "PASS"
        result["result"] = "PASS"
    except Exception as exc:
        result.update(result="FAIL", error=str(exc))
        raise
    finally:
        if pi_off_repair:
            try:
                probe.fault(drop_science_packet_once=-1)
            except Exception as exc:
                result.update(result="FAIL", cleanup_error=str(exc))
        result["completed_at"] = datetime.now(timezone.utc).isoformat()
        destination.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        print("Evidence:", destination)
    if result["result"] != "PASS":
        raise RuntimeError(result.get("cleanup_error", "Acceptance failed"))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", type=int, default=3)
    parser.add_argument("--pi-off-repair", action="store_true")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    if not 1 <= args.captures <= 20 or not 30 <= args.timeout <= 600:
        parser.error("captures must be 1..20 and timeout 30..600 seconds")
    run(args.captures, args.pi_off_repair, args.timeout)
