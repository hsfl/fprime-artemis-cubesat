#!/usr/bin/env python3
"""Probe existing collection + stock FileDownlink, commanded through Yamcs.

This does not exercise CommsApp's custom channel-1 science downlink. The local
source read is solely an integrity oracle; received bytes come from Yamcs GET.
"""
from datetime import datetime
import binascii
import fcntl
import hashlib
import json
from pathlib import Path
import secrets
import time
from urllib import error, parse, request

from smoke import ROOT, URL, INSTANCE, PROCESSOR, api, issue, wait_for_event

DEPLOYMENT = "ArtemisRpiTeensyDeployment"


def parameter(name):
    listing = api(f"/api/mdb/{INSTANCE}/parameters?" + parse.urlencode({"q": name.split(".")[-1], "limit": 500}))
    matches = [p["qualifiedName"] for p in listing.get("parameters", [])
               if p["qualifiedName"].endswith("/" + name.replace(".", "/"))]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one Yamcs parameter for {name}, found {matches}")
    return api(f"/api/processors/{INSTANCE}/{PROCESSOR}/parameters/" + parse.quote(matches[0], safe="/"))


def returned_event(command, component, event, predicate=None):
    released = datetime.fromisoformat(command["generationTime"].replace("Z", "+00:00"))
    expected = f"{DEPLOYMENT}.{component}.{event}"
    return wait_for_event(event, after=released, predicate=lambda e:
                          e.get("type") == expected and (predicate is None or predicate(e)))


def fresh_parameter(name, after, timeout=30, predicate=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = parameter(name)
            # A first retrieval can establish a Yamcs parameter-cache entry
            # before any live sample exists. Wait for acquisition instead of
            # treating that empty response as a test failure.
            if not value.get("acquisitionTime") or not value.get("generationTime"):
                time.sleep(0.5)
                continue
            acquired = datetime.fromisoformat(value["acquisitionTime"].replace("Z", "+00:00"))
            generated = datetime.fromisoformat(value["generationTime"].replace("Z", "+00:00"))
            if (value.get("acquisitionStatus") == "ACQUIRED" and acquired >= after and generated >= after
                    and (predicate is None or predicate(value))):
                return value
        except error.HTTPError as exc:
            if exc.code != 404:
                raise
        time.sleep(0.5)
    raise RuntimeError(f"No fresh acquired Yamcs parameter {name}")


def numeric(value):
    return int(value["engValue"]["uint32Value"])


def probe():
    current = json.loads((ROOT / "yamcs" / "run" / "current.json").read_text())
    run = Path(current["run_dir"])
    identifier = secrets.token_hex(6)
    evidence = run / f"collection-stock-file-{identifier}.json"
    result = {"result": "IN_PROGRESS", "route": "Yamcs -> existing simulated collection -> manual stock Fprime FileDownlink -> Yamcs bucket",
              "mission_science_channel_1_exercised": False}
    try:
        started = datetime.fromisoformat(current["started_at"])
        result["initial_mode"] = fresh_parameter(f"{DEPLOYMENT}.missionApp.CurrentMode", started)
        if result["initial_mode"].get("engValue", {}).get("stringValue") != "BASE":
            raise RuntimeError("Start a fresh local stack in BASE before this single-collection probe")
        result["configure"] = issue(f"{DEPLOYMENT}.scienceApp.CONFIGURE_CAPTURE_DURATION", {"durationMinutes": 1})
        result["configured_event"] = returned_event(result["configure"], "scienceApp", "CaptureDurationConfigured",
                                                    lambda e: e.get("extra", {}).get("durationMinutes") == "1")
        # The one-minute default may already be current when the command completes.
        result["configured_duration"] = fresh_parameter(f"{DEPLOYMENT}.scienceApp.CaptureDurationMinutes", started,
                                                       predicate=lambda v: numeric(v) == 1)
        # Require the initial collection count from this app startup.
        result["initial_collection_count"] = fresh_parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount", started)
        if numeric(result["initial_collection_count"]) != 0:
            raise RuntimeError("This probe requires a freshly started native app with no prior collections")
        result["schedule"] = issue(f"{DEPLOYMENT}.missionApp.SCHEDULE_COLLECTION", {"delaySeconds": 2})
        result["scheduled_event"] = returned_event(result["schedule"], "missionApp", "CollectionScheduled",
                                                  lambda e: e.get("extra", {}).get("delaySeconds") == "2")
        result["capture_event"] = returned_event(result["schedule"], "payloadDriverNeutronSim", "CaptureComplete",
                                                lambda e: e.get("extra", {}).get("durationMinutes") == "1"
                                                and e.get("extra", {}).get("productId") == "1")
        reported_bytes = int(result["capture_event"]["extra"]["productBytes"])
        result["stored_event"] = returned_event(result["schedule"], "storageManager", "ScienceStored",
                                               lambda e: e.get("extra", {}).get("productCount") == "1"
                                               and e.get("extra", {}).get("productBytes") == str(reported_bytes))
        result["ready_mode_event"] = returned_event(result["schedule"], "missionApp", "ModeChanged",
                                                   lambda e: e.get("extra", {}).get("mode") == "SCIENCE_READY")
        released = datetime.fromisoformat(result["schedule"]["generationTime"].replace("Z", "+00:00"))
        result["post_capture_mode"] = fresh_parameter(f"{DEPLOYMENT}.missionApp.CurrentMode", released,
                                                     predicate=lambda v: v["engValue"].get("stringValue") == "SCIENCE_READY")
        latest = Path("/tmp/neutron_payload_captures/latest_payload.bin")
        source = latest.resolve(strict=True)
        expected_bytes = source.read_bytes()
        if not expected_bytes or len(expected_bytes) != reported_bytes:
            raise RuntimeError("Fresh capture event and source integrity oracle disagree")
        result["product_id_telemetry"] = fresh_parameter(f"{DEPLOYMENT}.payloadDriverNeutronSim.LastProductId", released)
        result["product_crc_telemetry"] = fresh_parameter(f"{DEPLOYMENT}.payloadDriverNeutronSim.LastProductCrc", released)
        crc = binascii.crc_hqx(expected_bytes, 0xFFFF)
        if numeric(result["product_id_telemetry"]) != 1 or numeric(result["product_crc_telemetry"]) != crc:
            raise RuntimeError("Yamcs product identity/CRC does not match the source oracle")
        object_name = f"n2-local-{identifier}.csv"
        result["source_oracle"] = {"path": str(source.resolve(strict=True)), "bytes": len(expected_bytes),
                                   "sha256": hashlib.sha256(expected_bytes).hexdigest(), "crc16_ccitt": crc}
        # Never upload/copy the source to ground storage. F′ reads it and emits
        # Fw::FilePackets on CCSDS APID 3, reconstructed by the Yamcs service.
        result["downlink"] = issue("FileHandling.fileDownlink.SendFile",
                                   {"sourceFileName": str(source), "destFileName": object_name})
        endpoint = "/api/storage/buckets/fprimeFilesIn/objects/" + parse.quote(object_name, safe="") + "?instance=" + INSTANCE
        deadline = time.monotonic() + 40
        while True:
            try:
                with request.urlopen(URL + endpoint, timeout=10) as response:
                    received = response.read()
                break
            except error.HTTPError as exc:
                if exc.code != 404 or time.monotonic() > deadline:
                    raise
                time.sleep(0.5)
        destination = run / f"downloaded-{object_name}"
        destination.write_bytes(received)
        result["received_via_yamcs"] = {"endpoint": endpoint, "download_path": str(destination),
                                       "bytes": len(received), "sha256": hashlib.sha256(received).hexdigest()}
        if received != expected_bytes:
            raise RuntimeError("Yamcs-downloaded product differs from the fresh capture source")
        result["final_collection_count"] = fresh_parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount", released,
                                                          predicate=lambda v: numeric(v) == 1)
        if (latest.resolve(strict=True) != source or source.read_bytes() != expected_bytes
                or numeric(result["final_collection_count"]) != 1
                or numeric(parameter(f"{DEPLOYMENT}.payloadDriverNeutronSim.LastProductId")) != 1):
            raise RuntimeError("The source/product identity changed during the exclusive probe")
        result["result"] = "PASS"
    except Exception as exc:
        result["result"] = "FAIL"
        result["error"] = str(exc)
        raise
    finally:
        evidence.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        print(f"Evidence: {evidence}")


if __name__ == "__main__":
    # Serializes this tool's probes. Use an otherwise idle test stack; concurrent
    # manual operators are outside this diagnostic's admitted configuration.
    with (ROOT / "yamcs/run/demo.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        probe()
