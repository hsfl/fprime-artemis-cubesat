#!/usr/bin/env python3
"""Inspect CSV delivery, archive verified ground copies, and explicitly retire products."""
from __future__ import annotations

import argparse
import binascii
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import secrets
import shutil
import time
from urllib import parse

import stock_downlink as downlink
from local_file_receiver import file_sha256, publication_lock, read_receipt, valid_name
from yamcs_client import LocalYamcs, loopback_url


class RetirementUnknown(RuntimeError):
    """A confirmed retirement request may have executed; reconcile, never replay."""


def verified_csv(receipt_path: Path) -> tuple[dict, Path]:
    receipt = read_receipt(receipt_path)
    name = valid_name(receipt["destination"])
    if receipt_path.name != name + ".receipt.json" or not name.lower().endswith(".csv"):
        raise ValueError("select a CSV delivery receipt")
    if receipt.get("state") != "available_in_yamcs":
        raise ValueError("CSV must have verified Yamcs publication before archiving or retirement")
    source = receipt_path.parent / name
    if source.is_symlink():
        raise ValueError("local CSV must not be a symlink")
    size, digest = file_sha256(source)
    if (size != receipt["expected_size"] or size != receipt["received_size"] or
            digest != receipt["sha256"] or digest != receipt.get("bucket_sha256")):
        raise ValueError("local CSV or verification receipt does not match")
    return receipt, source


def archive(receive_dir: Path, destination: Path, confirm: bool) -> dict:
    receive_dir = receive_dir.resolve()
    destination = destination.resolve()
    if destination == receive_dir or receive_dir in destination.parents:
        raise ValueError("archive destination must be outside the active receive directory")
    entries, skipped = [], []
    for path in sorted(receive_dir.glob("*.receipt.json")):
        try:
            receipt, source = verified_csv(path)
            entries.append((path, source, receipt))
        except (OSError, ValueError, KeyError, TypeError) as error:
            skipped.append({"receipt": path.name, "reason": str(error)})
    result = {"mode": "archive" if confirm else "preview", "files": [s.name for _, s, _ in entries],
              "bytes": sum(r["expected_size"] for _, _, r in entries), "skipped": skipped}
    if not confirm or not entries:
        return result
    target = destination / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + secrets.token_hex(3))
    target.mkdir(parents=True)
    for receipt_path, source, _ in entries:
        with publication_lock(receipt_path):
            receipt, source = verified_csv(receipt_path)
            copied = target / source.name
            with source.open("rb") as incoming, copied.open("xb") as outgoing:
                shutil.copyfileobj(incoming, outgoing, 64 * 1024)
                outgoing.flush()
                os.fsync(outgoing.fileno())
            if file_sha256(copied) != (receipt["expected_size"], receipt["sha256"]):
                raise RuntimeError("archive readback mismatch; original retained")
            from local_file_receiver import atomic_json, sync_directory
            atomic_json(target / receipt_path.name, receipt)
            sync_directory(target)
            source.unlink()
            receipt_path.unlink()
            sync_directory(receive_dir)
        # A late publication retry sees the missing receipt and fails closed.
        receipt_path.with_suffix(receipt_path.suffix + ".publish.lock").unlink(missing_ok=True)
    result["archive_directory"] = str(target)
    return result


def flight_capacity(url: str, instance: str, processor: str) -> dict:
    result = {}
    listing = downlink.api(url, f"/api/mdb/{parse.quote(instance, safe='')}/parameters?" +
                           parse.urlencode({"q": "storageManager", "limit": 500}), timeout=2)
    for item in listing.get("parameters", []):
        name = item.get("qualifiedName", "")
        short = name.rsplit("/", 1)[-1]
        if short not in ("FreeProductSlots", "StoredProducts", "StorageReady"):
            continue
        value = downlink.api(url, f"/api/processors/{parse.quote(instance, safe='')}/"
                            f"{parse.quote(processor, safe='')}/parameters/{parse.quote(name, safe='/')}", timeout=2)
        result[short] = {"value": value.get("engValue", {}), "time": value.get("generationTime")}
    return result


def status(receive_dir: Path, url: str, instance: str, processor: str) -> dict:
    try:
        bridge = json.loads((receive_dir / "receiver-status.json").read_text())
        bridge["heartbeat_age_seconds"] = round(time.time() - float(bridge["heartbeat_unix"]), 1)
        if not -5 <= bridge["heartbeat_age_seconds"] <= 10:
            raise ValueError("bridge heartbeat is stale")
        pid = int(bridge["pid"])
        if pid <= 0:
            raise ValueError("invalid bridge process ID")
        if os.name != "nt":
            try:
                os.kill(pid, 0)
            except PermissionError:
                pass
    except (OSError, ValueError, KeyError, TypeError) as error:
        bridge = {"state": "unavailable", "file_admission_ready": False, "file_error": str(error)}
    files, bad = [], []
    for path in sorted(receive_dir.glob("*.receipt.json")):
        try:
            receipt = read_receipt(path)
            if not str(receipt.get("destination", "")).lower().endswith(".csv"):
                continue
            files.append({key: receipt.get(key) for key in (
                "destination", "source_path", "state", "received_size", "expected_size", "publication_error")})
            if receipt.get("state") == "saved_locally":
                files[-1]["retry_receipt"] = str(path)
        except (OSError, ValueError) as error:
            bad.append({"receipt": path.name, "error": str(error)})
    try:
        capacity = flight_capacity(url, instance, processor)
    except Exception as error:
        capacity = {"unavailable": str(error)}
    return {"bridge": bridge, "flight_storage": capacity, "csv_files": files, "bad_receipts": bad}


def retire(receipt_path: Path, product_id: int, confirm: bool, url: str, instance: str, processor: str) -> dict:
    receipt, source = verified_csv(receipt_path)
    selected = downlink.resolve_product(url, instance, processor, product_id, 15)
    extra = selected["event"]["extra"]
    crc = 0xFFFF
    with source.open("rb") as input_file:
        for block in iter(lambda: input_file.read(64 * 1024), b""):
            crc = binascii.crc_hqx(block, crc)
    if (selected["source_file"] != receipt["source_path"] or
            int(extra["productBytes"]) != receipt["expected_size"] or int(extra["sourceCrc"]) != crc):
        raise ValueError("selected onboard product differs from the verified ground CSV")
    try:
        command = downlink.issue(url, instance, processor, downlink.DEPLOYMENT + ".storageManager.RETIRE_PRODUCT",
                                 {"productId": product_id, "confirm": int(confirm)})
        released = downlink.stamp(command["generationTime"])
    except Exception as error:
        if confirm:
            raise RetirementUnknown(f"product {product_id} ({receipt['source_path']}) may be retired; "
                "ground CSV retained. Inspect catalog/events before another retirement command; no resend") from error
        raise RuntimeError("retirement preview unavailable; no confirmed retirement requested") from error
    expected = ".storageManager." + ("ProductRetired" if confirm else "ProductRetirementPreview")
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            events = downlink.archive_events(url, instance, "ProductRetire")
        except Exception as error:
            if confirm:
                raise RetirementUnknown(f"product {product_id} retirement could not be reconciled; "
                    "inspect catalog/events before another command; ground CSV retained") from error
            raise RuntimeError("retirement preview unavailable; no confirmed retirement requested") from error
        for event in events:
            if downlink.stamp(event["receptionTime"]) < released:
                continue
            if int(event.get("extra", {}).get("productId", -1)) != product_id:
                continue
            if event.get("type", "").endswith(".ProductRetirementFailed"):
                raise RuntimeError("flight rejected retirement; inspect storage events; no resend")
            if event.get("type", "").endswith(expected):
                return {"mode": "retired" if confirm else "preview", "product_id": product_id,
                        "verified_ground_copy": str(source), "event": event}
        time.sleep(0.25)
    if confirm:
        raise RetirementUnknown(f"product {product_id} retirement outcome is uncertain; "
                                "inspect catalog/events before another command; ground CSV retained")
    raise TimeoutError("retirement preview not observed; no confirmed retirement requested")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--station", type=Path, default=Path.home() / "yamcs-station")
    parser.add_argument("--receive-dir", type=Path)
    parser.add_argument("--url", type=loopback_url, default=downlink.DEFAULT_URL)
    parser.add_argument("--instance", default=downlink.DEFAULT_INSTANCE)
    parser.add_argument("--processor", default=downlink.DEFAULT_PROCESSOR)
    parser.add_argument("--json", action="store_true")
    commands = parser.add_subparsers(dest="action", required=True)
    commands.add_parser("status")
    archiver = commands.add_parser("archive")
    archiver.add_argument("--destination", required=True, type=Path)
    archiver.add_argument("--confirm", action="store_true", help="copy/verify then remove archived active-spool files")
    retiring = commands.add_parser("retire")
    retiring.add_argument("--product-id", required=True, type=int)
    retiring.add_argument("--receipt", required=True, type=Path)
    retiring.add_argument("--confirm", action="store_true", help="explicitly remove the selected onboard product")
    args = parser.parse_args(argv)
    station = args.station.expanduser().resolve()
    receive_dir = (args.receive_dir or station / "received-files").expanduser().resolve()
    credentials = station / "Yamcs-access.txt"
    downlink._authenticated_client = LocalYamcs(args.url, credentials) if credentials.is_file() else None
    try:
        if args.action == "status":
            result = status(receive_dir, args.url, args.instance, args.processor)
        elif args.action == "archive":
            result = archive(receive_dir, args.destination.expanduser(), args.confirm)
        else:
            if args.product_id <= 0:
                raise ValueError("product ID must be positive")
            result = retire(args.receipt.expanduser(), args.product_id, args.confirm,
                            args.url, args.instance, args.processor)
        if args.json or args.action != "status":
            print(json.dumps(result, indent=2))
        else:
            bridge = result["bridge"]
            print(f"Bridge: {bridge.get('state')} | file admission: {bridge.get('file_admission_ready', False)} "
                  f"| heartbeat age: {bridge.get('heartbeat_age_seconds', 'unknown')}s")
            print(f"Storage: {json.dumps(result['flight_storage'])}")
            print(f"Spool headroom: {bridge.get('free_spool_bytes', 'unknown')} bytes")
            if bridge.get("file_error"):
                print("File fault: " + str(bridge["file_error"]))
            for item in result["csv_files"]:
                print(f"{item['state']}: {item['destination']} {item['received_size']}/{item['expected_size']} bytes")
                if item.get("retry_receipt"):
                    print("  Publish retry receipt: " + item["retry_receipt"])
        return 0
    except RetirementUnknown as error:
        print(f"UNKNOWN: {error}")
        return 2
    except Exception as error:
        print(f"FAIL: {error}")
        return 1
    finally:
        downlink._authenticated_client = None


if __name__ == "__main__":
    raise SystemExit(main())
