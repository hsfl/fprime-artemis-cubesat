#!/usr/bin/env python3
"""Accept B through Yamcs commands/events and independently downloaded science.

The sole flight-source read below is an integrity oracle AFTER channel-1 receipt.
It is never supplied to the receiver, uploaded, or used by the ground histogram.
"""
import argparse
import binascii
import csv
from datetime import datetime
import fcntl
import hashlib
import io
import json
from pathlib import Path
import secrets
import time
from urllib import parse, request

from demo import returned_event, numeric, fresh_parameter
from passthrough import parameter, wait_for_running, mode_value
from smoke import ROOT, URL, INSTANCE, api, issue, wait_for_event, events

DEPLOYMENT = "ArtemisRpiTeensyDeployment"


def download(object_name):
    endpoint = "/api/storage/buckets/fprimeFilesIn/objects/" + parse.quote(object_name, safe="")
    with request.urlopen(URL + endpoint + "?instance=" + INSTANCE, timeout=10) as response:
        return response.read()


def probe(durations):
    current = json.loads((ROOT / "yamcs/run/current.json").read_text())
    run = Path(current["run_dir"])
    evidence = run / f"raw-cycle-{secrets.token_hex(6)}.json"
    result = {"result": "IN_PROGRESS", "hardware_or_rf_used": False,
              "route": "Yamcs -> ScienceApp.COLLECT_AND_DOWNLINK -> simulated capture -> validated storage -> CommsApp -> PayloadDownlinkApp -> UART channel 1 -> independent ground receiver -> Yamcs objects -> ground histogram",
              "stock_file_downlink_used": False, "captures": [],
              "binary_sha256": current["binary_sha256"], "dictionary_sha256": current["dictionary_sha256"]}
    sources = ["science_ingest.py", "science_viewer.py", "local_link.py", "local_stack.py", "raw_cycle.py"]
    result["ground_source_sha256"] = {name: hashlib.sha256((ROOT / "tools/yamcs" / name).read_bytes()).hexdigest()
                                      for name in sources}
    try:
        wait_for_running()
        result["initial_mode"] = mode_value(datetime.fromisoformat(current["started_at"]))
        if result["initial_mode"] != "BASE":
            raise RuntimeError("Use an idle local stack in BASE")
        initial_count = numeric(parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount"))
        result["initial_collection_count"] = initial_count
        result["negative_checks"] = []
        for duration, fault, expected in [(0, None, "ScienceCommandRejected"), (2, "FAIL", "ScienceOperationRejected")]:
            if fault:
                configured = issue(f"{DEPLOYMENT}.payloadManager.CONFIGURE_SIMULATION",
                    {"faultMode": fault, "delayMs": 0, "timeoutMs": 8000})
                returned_event(configured, "payloadManager", "SimulationConfigured",
                    lambda event: event.get("extra", {}).get("faultMode") == fault)
            command = issue(f"{DEPLOYMENT}.scienceApp.COLLECT_AND_DOWNLINK", {"durationMinutes": duration})
            rejected = returned_event(command, "scienceApp", expected)
            released = datetime.fromisoformat(command["generationTime"].replace("Z", "+00:00"))
            time.sleep(3)
            fresh_parameter(f"{DEPLOYMENT}.payloadManager.ManagerHeartbeat", released)
            forbidden = []
            for name in ("ScienceStored", "PayloadDownlinkStarted", "GroundScienceReceiptVerified"):
                forbidden.extend(event for event in events(name)
                    if datetime.fromisoformat(event["receptionTime"].replace("Z", "+00:00")) >= released)
            if forbidden or numeric(parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount")) != initial_count:
                raise RuntimeError("Invalid/failed automatic capture published or downlinked a product")
            result["negative_checks"].append({"duration_minutes": duration, "fault": fault,
                "command": command, "rejected": rejected, "result": "PASS", "forbidden_events": forbidden})
        for index, duration in enumerate(durations):
            capture = {"duration_minutes": duration}
            result["captures"].append(capture)
            if index == 0:
                configured = issue(f"{DEPLOYMENT}.payloadManager.CONFIGURE_SIMULATION",
                    {"faultMode": "DELAYED_RESPONSE", "delayMs": 4000, "timeoutMs": 8000})
                returned_event(configured, "payloadManager", "SimulationConfigured",
                    lambda event: event.get("extra", {}).get("faultMode") == "DELAYED_RESPONSE")
            command = issue(f"{DEPLOYMENT}.scienceApp.COLLECT_AND_DOWNLINK", {"durationMinutes": duration})
            capture["command"] = command
            released = datetime.fromisoformat(command["generationTime"].replace("Z", "+00:00"))
            capture["admitted"] = returned_event(command, "payloadManager", "PayloadOperationStatus",
                lambda event: event.get("extra", {}).get("state") == "ACCEPTED"
                and event.get("extra", {}).get("kind") == "CAPTURE")
            if index == 0:
                owner_id = int(capture["admitted"]["extra"]["requestId"])
                contender = issue(f"{DEPLOYMENT}.payloadManager.START_PAYLOAD_COLLECTION", {"collectionId": owner_id})
                busy = returned_event(contender, "payloadManager", "PayloadOperationStatus",
                    lambda event: event.get("extra", {}).get("state") == "BUSY"
                    and event.get("extra", {}).get("requestId") == str(owner_id)
                    and event.get("extra", {}).get("generation") == "0")
                capture["same_id_busy_contender"] = {"command": contender, "rejected": busy}
            capture["capture_complete"] = returned_event(command, "payloadDriverNeutronSim", "CaptureComplete",
                lambda event: event.get("extra", {}).get("durationMinutes") == str(duration))
            product = int(capture["capture_complete"]["extra"]["productId"])
            size = int(capture["capture_complete"]["extra"]["productBytes"])
            capture["stored"] = returned_event(command, "storageManager", "ScienceStored",
                lambda event: event.get("extra", {}).get("productBytes") == str(size))
            capture["sender_started"] = returned_event(command, "payloadDownlinkApp", "PayloadDownlinkStarted",
                lambda event: event.get("extra", {}).get("productId") == str(product))
            transfer = int(capture["sender_started"]["extra"]["transferId"])
            capture["sender_complete"] = wait_for_event("PayloadDownlinkComplete", timeout=90, after=released,
                predicate=lambda event: event.get("type") == f"{DEPLOYMENT}.payloadDownlinkApp.PayloadDownlinkComplete"
                and event.get("extra", {}).get("transferId") == str(transfer))
            capture["ground_receipt_event"] = wait_for_event("GroundScienceReceiptVerified", timeout=90, after=released,
                predicate=lambda event: event.get("type") == "GroundScienceReceiptVerified"
                and event.get("extra", {}).get("productId") == str(product))
            extra = capture["ground_receipt_event"]["extra"]
            receipt = json.loads(download(extra["receiptObject"]))
            received = download(receipt["objectName"])
            capture["receipt"] = receipt
            received_hash = hashlib.sha256(received).hexdigest()
            if (receipt["runId"] != current["local_run_id"] or receipt["productId"] != product
                    or receipt["transferId"] != transfer or receipt["bytes"] != size or len(received) != size
                    or not receipt["verifiedGroundReceipt"] or receipt["sha256"] != received_hash
                    or receipt["crc16Ccitt"] != binascii.crc_hqx(received, 0xFFFF)):
                raise RuntimeError("Ground receipt identity/size/CRC/SHA verification failed")
            destination = run / ("independently-downloaded-" + receipt["objectName"])
            destination.write_bytes(received)
            capture["yamcs_download"] = {"path": str(destination), "bytes": len(received), "sha256": received_hash}
            # Read only after ground receipt: a source oracle cannot drive ingest.
            source = Path("/tmp/neutron_payload_captures/latest_payload.bin").resolve(strict=True)
            source_bytes = source.read_bytes()
            if (source_bytes != received or numeric(parameter(f"{DEPLOYMENT}.payloadDriverNeutronSim.LastProductId")) != product):
                raise RuntimeError("Independent ground bytes differ from the matching finalized source oracle")
            capture["source_oracle"] = {"path": str(source), "bytes": len(source_bytes),
                                         "sha256": hashlib.sha256(source_bytes).hexdigest()}
            rows = list(csv.DictReader(io.StringIO(received.decode("utf-8"))))
            if len(rows) != duration * 60 or any(set(row) != {"t_s", "counts", "flag"} for row in rows):
                raise RuntimeError("Received simulator CSV does not match the requested windows")
            frequencies = {}
            for row in rows:
                count = int(row["counts"])
                frequencies[str(count)] = frequencies.get(str(count), 0) + 1
            capture["histogram_oracle"] = {"windows": len(rows), "totalCounts": sum(int(r["counts"]) for r in rows),
                                           "frequencies": frequencies, "source": "independent Yamcs download"}
            # The viewer independently fetches the same Yamcs object; compare its derivation.
            with request.urlopen(current["science_viewer_url"] + "/api/product?" + parse.urlencode({"receipt": extra["receiptObject"]}), timeout=10) as response:
                displayed = json.load(response)
            capture["ground_viewer"] = displayed
            if (displayed["objectName"] != receipt["objectName"]
                    or displayed["sha256"] != received_hash or displayed["readbackSha256"] != received_hash
                    or displayed["productId"] != product
                    or displayed["transferId"] != transfer
                    or displayed["histogram"]["windows"] != len(rows)
                    or displayed["histogram"]["totalCounts"] != capture["histogram_oracle"]["totalCounts"]
                    or displayed["histogram"]["frequencies"] != frequencies):
                raise RuntimeError("Ground histogram disagrees with independently downloaded raw data")
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if mode_value() == "BASE":
                    break
                time.sleep(0.25)
            else:
                raise RuntimeError("Sender did not return to BASE")
            capture["collection_count"] = fresh_parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount", released,
                predicate=lambda value: numeric(value) == initial_count + index + 1)
            capture["result"] = "PASS"
        if len({c["receipt"]["objectName"] for c in result["captures"]}) != len(durations):
            raise RuntimeError("Distinct products overwrote a ground object")
        # Original raw files are retained after local sending and ground receipt.
        result["retained_sources"] = [c["source_oracle"]["path"] for c in result["captures"]]
        if any(not Path(path).is_file() for path in result["retained_sources"]):
            raise RuntimeError("A source disappeared after sender completion")
        result["configured_ground_faults"] = current.get("local_science_fault_profile", [])
        if result["configured_ground_faults"]:
            faults = result["captures"][0]["receipt"]["faults"]
            if not (faults["packetsDropped"] >= 1 and faults["packetsCorrupted"] >= 1
                    and faults["retryRequestsSent"] >= 1
                    and result["captures"][0]["receipt"]["groundIngestCounters"]["recoveredPackets"] >= 2):
                raise RuntimeError("Configured ground faults were not exercised and repaired")
        if any(hashlib.sha256((ROOT / "tools/yamcs" / name).read_bytes()).hexdigest() != digest
               for name, digest in result["ground_source_sha256"].items()):
            raise RuntimeError("Ground source changed during acceptance")
        result["result"] = "PASS"
    except Exception as exc:
        result["result"], result["error"] = "FAIL", str(exc)
        raise
    finally:
        result["finished_at"] = datetime.now().astimezone().isoformat()
        evidence.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        print(f"Evidence: {evidence}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--durations", type=int, choices=(1, 2), nargs="+", default=[1, 2],
                        help="Simulated capture durations in minutes")
    args = parser.parse_args()
    with (ROOT / "yamcs/run/demo.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        probe(args.durations)
