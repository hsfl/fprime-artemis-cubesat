#!/usr/bin/env python3
"""Accept synthetic pulse science through Yamcs and independent raw readback."""
import binascii
from datetime import datetime
import fcntl
import hashlib
import json
import math
from pathlib import Path
import secrets
import time
from urllib import parse, request

from demo import returned_event, numeric, fresh_parameter
from passthrough import parameter, wait_for_running, mode_value
from raw_cycle import download
from smoke import ROOT, issue, wait_for_event, events

DEPLOYMENT = "ArtemisRpiTeensyDeployment"
SOURCES = ["pulse_cycle.py", "pulse_analysis.py", "science_viewer.py", "science_ingest.py",
           "local_link.py", "local_stack.py", "pulse_sim/neutron_payload_sim.py"]


def fingerprints():
    return {name: hashlib.sha256((ROOT / "tools/yamcs" / name).read_bytes()).hexdigest()
            for name in SOURCES}


def check_science(raw, displayed, duration_minutes):
    """Independent calculations; deliberately does not import ground analysis."""
    data = json.loads(raw)
    science = displayed["science"]
    assert data["schema"] == science["schema"] == "n2-pulses-v1"
    assert data["simulated"] is science["simulated"] is True
    assert data["durationSeconds"] == duration_minutes * 60
    expected_count = duration_minutes * 60 * 2
    assert len(data["events"]) == science["eventCount"] == expected_count
    charges, ratios = [], []
    for event, actual, wave in zip(data["events"], science["events"], science["waveforms"], strict=True):
        baseline = sum(event["samples"][:data["baselineSamples"]]) / data["baselineSamples"]
        residual = [max(0.0, value - baseline) for value in event["samples"]]
        total = sum(residual[data["integrationStart"]:data["integrationEnd"]])
        tail = sum(residual[data["tailStart"]:data["integrationEnd"]])
        ratio = tail / total
        assert actual["id"] == event["id"] and actual["channel"] == event["channel"]
        assert actual["timestampNs"] == event["timestampNs"]
        assert math.isclose(actual["totalCharge"], total, rel_tol=1e-12)
        assert math.isclose(actual["tailCharge"], tail, rel_tol=1e-12)
        assert math.isclose(actual["psd"], ratio, rel_tol=1e-12)
        assert wave["eventId"] == event["id"] and wave["rawSamples"] == event["samples"]
        assert wave["baseline"] == baseline and wave["baselineCorrectedSamples"] == residual
        assert wave["sampleTimesNs"] == [i * data["sampleIntervalNs"] for i in range(64)]
        charges.append(total)
        ratios.append(ratio)
    spectrum = science["chargeSpectrum"]
    density = science["psdDensity"]
    bins = spectrum["binCount"]
    assert bins == density["chargeBinCount"] == density["psdBinCount"] == 32
    assert spectrum["minimum"] == density["chargeMinimum"] == 0
    assert spectrum["maximum"] == density["chargeMaximum"]
    assert math.isclose(spectrum["maximum"], max(charges) * 1.05, rel_tol=1e-12)
    assert math.isclose(spectrum["binWidth"], spectrum["maximum"] / bins, rel_tol=1e-12)
    assert density["psdMinimum"] == 0 and density["psdMaximum"] == 1
    expected_bins = [0] * bins
    expected_grid = [[0] * bins for _ in range(bins)]
    for total, ratio in zip(charges, ratios, strict=True):
        x = min(bins - 1, int(total / spectrum["maximum"] * bins))
        y = min(bins - 1, int(ratio * bins))
        expected_bins[x] += 1
        expected_grid[y][x] += 1
    assert spectrum["counts"] == expected_bins and density["counts"] == expected_grid
    assert sum(expected_bins) == sum(map(sum, expected_grid)) == expected_count
    assert min(ratios) < max(ratios)
    return {"event_count": expected_count, "waveforms_checked": expected_count,
            "spectrum_counts": expected_bins, "psd_density_counts": expected_grid,
            "psd_range": [min(ratios), max(ratios)],
            "source": "independent calculations from Yamcs-downloaded raw samples"}


def probe():
    current = json.loads((ROOT / "yamcs/run/current.json").read_text())
    if current.get("local_science_format") != "pulses-v1":
        raise RuntimeError("Start a fresh pulse stack with tools/yamcs/run_pulses.sh")
    if current.get("local_source_sha256") != fingerprints():
        raise RuntimeError("Sources differ from stack startup; restart the pulse stack before acceptance")
    run = Path(current["run_dir"])
    result = {"result": "IN_PROGRESS", "hardware_or_rf_used": False,
              "simulated_science": True, "fom_or_calibration_used": False,
              "run_id": current["local_run_id"], "captures": [], "negative_checks": [],
              "binary_sha256": current["binary_sha256"],
              "dictionary_sha256": current["dictionary_sha256"], "source_sha256": fingerprints()}
    evidence = run / f"pulse-cycle-{secrets.token_hex(6)}.json"
    try:
        wait_for_running()
        assert mode_value() == "BASE"
        initial = numeric(parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount"))
        for duration, fault, expected in [(0, None, "ScienceCommandRejected"), (2, "FAIL", "ScienceOperationRejected")]:
            if fault:
                configured = issue(f"{DEPLOYMENT}.payloadManager.CONFIGURE_SIMULATION",
                                   {"faultMode": fault, "delayMs": 0, "timeoutMs": 8000})
                returned_event(configured, "payloadManager", "SimulationConfigured")
            command = issue(f"{DEPLOYMENT}.scienceApp.COLLECT_AND_DOWNLINK", {"durationMinutes": duration})
            rejected = returned_event(command, "scienceApp", expected)
            released = datetime.fromisoformat(command["generationTime"].replace("Z", "+00:00"))
            time.sleep(3)
            fresh_parameter(f"{DEPLOYMENT}.payloadManager.ManagerHeartbeat", released)
            forbidden = [e for name in ("ScienceStored", "PayloadDownlinkStarted", "GroundScienceReceiptVerified")
                         for e in events(name)
                         if datetime.fromisoformat(e["receptionTime"].replace("Z", "+00:00")) >= released]
            assert not forbidden and numeric(parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount")) == initial
            result["negative_checks"].append({"command": command, "rejected": rejected, "result": "PASS"})
        for index, duration in enumerate((1, 2)):
            command = issue(f"{DEPLOYMENT}.scienceApp.COLLECT_AND_DOWNLINK", {"durationMinutes": duration})
            released = datetime.fromisoformat(command["generationTime"].replace("Z", "+00:00"))
            capture = {"command": command, "duration_minutes": duration}
            result["captures"].append(capture)
            capture["admitted"] = returned_event(command, "payloadManager", "PayloadOperationStatus",
                lambda e: e.get("extra", {}).get("state") == "ACCEPTED" and e["extra"].get("kind") == "CAPTURE")
            capture["capture_complete"] = returned_event(command, "payloadDriverNeutronSim", "CaptureComplete",
                lambda e: e.get("extra", {}).get("durationMinutes") == str(duration))
            product = int(capture["capture_complete"]["extra"]["productId"])
            size = int(capture["capture_complete"]["extra"]["productBytes"])
            capture["stored"] = returned_event(command, "storageManager", "ScienceStored",
                lambda e: e.get("extra", {}).get("productBytes") == str(size))
            capture["sender_started"] = returned_event(command, "payloadDownlinkApp", "PayloadDownlinkStarted",
                lambda e: e.get("extra", {}).get("productId") == str(product))
            transfer = int(capture["sender_started"]["extra"]["transferId"])
            capture["sender_complete"] = wait_for_event("PayloadDownlinkComplete", timeout=120, after=released,
                predicate=lambda e: e.get("extra", {}).get("transferId") == str(transfer))
            capture["ground_receipt_event"] = wait_for_event("GroundScienceReceiptVerified", timeout=120, after=released,
                predicate=lambda e: e.get("extra", {}).get("productId") == str(product)
                and e["extra"].get("runId") == current["local_run_id"])
            extra = capture["ground_receipt_event"]["extra"]
            receipt = json.loads(download(extra["receiptObject"]))
            raw = download(receipt["objectName"])
            digest = hashlib.sha256(raw).hexdigest()
            assert (receipt["runId"] == current["local_run_id"] and receipt["productId"] == product
                    and receipt["transferId"] == transfer and receipt["bytes"] == len(raw) == size
                    and receipt["sha256"] == digest and receipt["crc16Ccitt"] == binascii.crc_hqx(raw, 0xFFFF)
                    and receipt["verifiedGroundReceipt"] is True)
            destination = run / ("independently-downloaded-" + receipt["objectName"])
            destination.write_bytes(raw)
            source = Path("/tmp/neutron_payload_captures/latest_payload.bin").resolve(strict=True)
            assert source.read_bytes() == raw  # Integrity oracle only AFTER ground receipt.
            with request.urlopen(current["science_viewer_url"] + "/api/product?" +
                                 parse.urlencode({"receipt": extra["receiptObject"]}), timeout=10) as response:
                displayed = json.load(response)
            assert displayed["readbackSha256"] == digest and displayed["scienceType"] == "pulses"
            capture.update({"receipt": receipt, "download": str(destination),
                            "source_oracle_after_receipt": str(source),
                            "plot_verification": check_science(raw, displayed, duration), "result": "PASS"})
            fresh_parameter(f"{DEPLOYMENT}.scienceApp.CollectionCount", released,
                            predicate=lambda value: numeric(value) == initial + index + 1)
        assert len({c["receipt"]["objectName"] for c in result["captures"]}) == 2
        assert all(Path(c["source_oracle_after_receipt"]).is_file() for c in result["captures"])
        result["configured_ground_faults"] = current["local_science_fault_profile"]
        if result["configured_ground_faults"]:
            first = result["captures"][0]["receipt"]
            assert first["faults"]["packetsDropped"] == first["faults"]["packetsCorrupted"] == 1
            assert first["faults"]["retryRequestsSent"] >= 1
            assert first["groundIngestCounters"]["recoveredPackets"] >= 2
        assert fingerprints() == result["source_sha256"]
        result["result"] = "PASS"
    except Exception as exc:
        result["result"], result["error"] = "FAIL", repr(exc)
        raise
    finally:
        result["finished_at"] = datetime.now().astimezone().isoformat()
        evidence.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({"result": result["result"], "evidence": str(evidence)}))


if __name__ == "__main__":
    with (ROOT / "yamcs/run/demo.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        probe()
