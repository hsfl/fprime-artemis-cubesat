#!/usr/bin/env python3
"""Send stock FileDownlink once and verify local receiver plus Yamcs bucket receipt."""

from __future__ import annotations

import argparse
import binascii
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import secrets
import time
from urllib import error, parse, request

DEFAULT_URL = "http://127.0.0.1:8090"
DEFAULT_INSTANCE = "fprime-project"
DEFAULT_PROCESSOR = "realtime"
DEPLOYMENT = "ArtemisRpiTeensyDeployment"
BUCKET = "fprimeFilesIn"
FILE_ERRORS = (
    "FileOpenError", "FileReadError", "SendDataFail", "DownlinkZeroSizeFile",
    "DownlinkCanceled", "DownlinkPartialFail", "FilenameSourceOverflow",
    "FilenameDestinationOverflow",
)
_authenticated_client = None


class IntegrityMismatch(RuntimeError):
    """Received bytes failed an independent source/catalog/receipt check."""


def api(url: str, path: str, body: dict | None = None, timeout: float = 10.0):
    if _authenticated_client is not None:
        return _authenticated_client.api(path, body, timeout=timeout)
    headers = {} if body is None else {"Content-Type": "application/json"}
    data = None if body is None else json.dumps(body).encode("utf-8")
    req = request.Request(url.rstrip("/") + path, data=data, headers=headers,
                          method="GET" if body is None else "POST")
    with request.urlopen(req, timeout=timeout) as response:
        payload = response.read()
    if not payload:
        return {}
    return json.loads(payload)


def api_bytes(url: str, path: str, timeout: float = 10.0) -> bytes:
    if _authenticated_client is not None:
        return _authenticated_client.api_bytes(path, timeout=timeout)
    req = request.Request(url.rstrip("/") + path, method="GET")
    with request.urlopen(req, timeout=timeout) as response:
        return response.read()


def stamp(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def issue(url: str, instance: str, processor: str, command_name: str, args: dict) -> dict:
    short_name = command_name.split(".")[-1]
    listing = api(url, f"/api/mdb/{parse.quote(instance, safe='')}/commands?" +
                  parse.urlencode({"q": short_name, "limit": 500}))
    suffix = "/" + command_name.replace(".", "/")
    matches = [row["qualifiedName"] for row in listing.get("commands", [])
               if row.get("qualifiedName", "").endswith(suffix)]
    if len(matches) != 1:
        raise RuntimeError(f"expected one Yamcs command {command_name}, found {matches}")
    command_path = (f"/api/processors/{parse.quote(instance, safe='')}/"
                    f"{parse.quote(processor, safe='')}/commands/"
                    f"{parse.quote(matches[0], safe='/')}")
    body = {"args": args, "origin": "n2-cdr-stock-filedownlink",
            "sequenceNumber": secrets.randbelow(2_147_483_646) + 1}
    return api(url, command_path, body)


def archive_events(url: str, instance: str, text: str) -> list[dict]:
    path = (f"/api/archive/{parse.quote(instance, safe='')}/events?" +
            parse.urlencode({"q": text, "limit": 200}))
    result = api(url, path)
    return result.get("events", result.get("event", []))


def event_matches(event: dict, source: str, destination: str, released: datetime) -> bool:
    reception = event.get("receptionTime")
    if not reception:
        return False
    try:
        if stamp(reception) < released:
            return False
    except ValueError:
        return False
    event_type = str(event.get("type", ""))
    extra = {key: str(value) for key, value in event.get("extra", {}).items()}
    matched_name = any(value in (source, destination) for value in extra.values())
    return any(event_type.endswith("." + name) for name in FILE_ERRORS) and (matched_name or
           event_type.endswith("FilenameSourceOverflow") or
           event_type.endswith("FilenameDestinationOverflow"))


def resolve_product(url: str, instance: str, processor: str, product_id: int,
                   timeout: float) -> dict:
    selected_command = issue(
        url, instance, processor, f"{DEPLOYMENT}.storageManager.SELECT_PRODUCT",
        {"productId": product_id},
    )
    released = stamp(selected_command["generationTime"])
    expected_type = f"{DEPLOYMENT}.storageManager.ProductSelected"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for event in archive_events(url, instance, "ProductSelected"):
            if event.get("type") != expected_type:
                continue
            if stamp(event["receptionTime"]) < released:
                continue
            extra = event.get("extra", {})
            if int(extra.get("productId", -1)) == product_id:
                source = str(extra.get("sourcePath", ""))
                if not source:
                    raise RuntimeError(f"ProductSelected for {product_id} omitted sourcePath")
                return {"command": selected_command, "event": event, "source_file": source}
        time.sleep(0.25)
    raise TimeoutError(f"no fresh ProductSelected event for product {product_id} within {timeout:g}s")


def source_bytes_limit(source: str, label: str) -> None:
    length = len(source.encode("utf-8"))
    if length == 0 or length > 100:
        raise ValueError(f"{label} must be 1 to 100 UTF-8 bytes for this FileDownlink dictionary; got {length}")


def parse_nonnegative_integer(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError:
        return int(value, 10)


def integrity_expectations(args: argparse.Namespace, selected: dict | None,
                           expected_file: bytes | None) -> dict:
    """Build mandatory receipt checks from a local oracle or Yamcs product metadata."""
    expected_size = args.expected_size
    expected_sha = args.expected_sha256.lower() if args.expected_sha256 else None
    expected_crc = args.expected_crc16
    if expected_size is not None and expected_size <= 0:
        raise ValueError("--expected-size must be positive")
    if expected_crc is not None and not 0 <= expected_crc <= 0xFFFF:
        raise ValueError("--expected-crc16 must be between 0 and 65535")
    if expected_sha and (len(expected_sha) != 64 or any(c not in "0123456789abcdef" for c in expected_sha)):
        raise ValueError("--expected-sha256 must contain exactly 64 hexadecimal characters")

    if selected is not None:
        fields = selected["event"].get("extra", {})
        try:
            product_size = int(fields["productBytes"])
            product_crc = int(fields["sourceCrc"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError("ProductSelected omitted valid productBytes/sourceCrc integrity metadata") from exc
        if product_size <= 0 or not 0 <= product_crc <= 0xFFFF:
            raise ValueError(f"ProductSelected has invalid size/CRC: {product_size}/{product_crc}")
        if expected_size is not None and expected_size != product_size:
            raise ValueError(f"--expected-size {expected_size} conflicts with selected product size {product_size}")
        if expected_crc is not None and expected_crc != product_crc:
            raise ValueError(f"--expected-crc16 {expected_crc} conflicts with selected product CRC {product_crc}")
        expected_size = product_size
        expected_crc = product_crc

    file_sha = None
    file_crc = None
    if expected_file is not None:
        file_sha = hashlib.sha256(expected_file).hexdigest()
        file_crc = binascii.crc_hqx(expected_file, 0xFFFF)
        if expected_size is not None and expected_size != len(expected_file):
            raise ValueError("expected size conflicts with --expected-file or selected product metadata")
        if expected_sha is not None and expected_sha != file_sha:
            raise ValueError("--expected-sha256 conflicts with --expected-file")
        if expected_crc is not None and expected_crc != file_crc:
            raise ValueError("expected CRC conflicts with --expected-file or selected product metadata")
        expected_size = len(expected_file)
        expected_sha = file_sha
        expected_crc = file_crc

    if selected is None and expected_file is None:
        if expected_size is None or (expected_sha is None and expected_crc is None):
            raise ValueError("source-file mode requires --expected-file or --expected-size plus --expected-sha256/--expected-crc16")

    return {
        "expected_size": expected_size,
        "expected_sha256": expected_sha,
        "expected_crc16_ccitt": expected_crc,
        "expected_file_bytes": expected_file,
        # A supplied digest is a check value, but the helper has independently
        # observed the source bytes only when a local oracle file was provided.
        "source_sha256_verified": expected_file is not None,
    }


def verify_received(received: bytes, expectations: dict) -> dict:
    return {
        "nonempty": len(received) > 0,
        "expected_size": len(received) == expectations["expected_size"],
        "expected_sha256": (expectations["expected_sha256"] is None or
                            hashlib.sha256(received).hexdigest() == expectations["expected_sha256"]),
        "expected_crc16_ccitt": (expectations["expected_crc16_ccitt"] is None or
                                  binascii.crc_hqx(received, 0xFFFF) == expectations["expected_crc16_ccitt"]),
        "expected_file_bytes": (expectations["expected_file_bytes"] is None or
                                received == expectations["expected_file_bytes"]),
    }


def object_endpoint(instance: str, object_name: str) -> str:
    return (f"/api/storage/buckets/{parse.quote(BUCKET, safe='')}/objects/"
            f"{parse.quote(object_name, safe='')}?" +
            parse.urlencode({"instance": instance}))


def existing_object(url: str, instance: str, object_name: str) -> bool:
    try:
        api_bytes(url, object_endpoint(instance, object_name), timeout=5)
        return True
    except error.HTTPError as exc:
        if exc.code == 404:
            return False
        raise


def downlink_error(url: str, instance: str, source: str, destination: str,
                   released: datetime) -> dict | None:
    for event_name in FILE_ERRORS:
        for event in archive_events(url, instance, event_name):
            if event_matches(event, source, destination, released):
                return event
    return None


def default_destination(source: str, product_id: int | None = None) -> str:
    if product_id is not None:
        basename = source.replace("\\", "/").rsplit("/", 1)[-1]
        stem = Path(basename).stem
        if not stem.startswith("daq_"):
            stem = f"science-product-{product_id}"
        return f"{stem}-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}-{secrets.token_hex(3)}.csv"
    basename = source.replace("\\", "/").rsplit("/", 1)[-1] or "science.bin"
    basename = basename[-64:]
    result = f"cdr-{secrets.token_hex(4)}-{Path(basename).stem}.csv"
    source_bytes_limit(result, "destination file name")
    return result


def require_receiver_ready(receive_dir: Path) -> dict:
    """The disk receiver, rather than native transfer rows, owns admission."""
    try:
        status = json.loads((receive_dir / "receiver-status.json").read_text())
        age = time.time() - float(status["heartbeat_unix"])
        if not -5 <= age <= 10:
            raise RuntimeError("local receiver heartbeat is stale; start/check the ground bridge")
        if status.get("state") != "ready":
            raise RuntimeError(f"local receiver is {status.get('state')}: {status.get('destination') or ''}")
        if not status.get("file_admission_ready", True):
            raise RuntimeError("file reception unavailable: " + str(status.get("file_error", "spool fault")))
        pid = status["pid"]
        if type(pid) is not int or pid <= 0:
            raise ValueError("receiver PID must be a positive integer")
        # Signal zero is a POSIX liveness probe. Windows os.kill uses
        # TerminateProcess for signals other than its supported console events.
        if os.name != "nt":
            os.kill(pid, 0)
    except (OSError, KeyError, ValueError) as exc:
        raise RuntimeError("local receiver is unavailable; start/check the ground bridge") from exc
    return status


def await_local_receipt(receive_dir: Path, source: str, destination: str,
                        released: float, timeout: float, idle_timeout: float,
                        url: str, instance: str, result: dict) -> dict:
    path = receive_dir / (destination + ".receipt.json")
    deadline = time.monotonic() + timeout
    last_progress = time.monotonic()
    previous_size = -1
    next_error_check = 0.0
    while time.monotonic() < deadline:
        try:
            receipt = json.loads(path.read_text())
        except FileNotFoundError:
            receipt = None
        if receipt is not None:
            if (receipt.get("destination") != destination or receipt.get("source_path") != source
                    or float(receipt.get("started_unix", 0)) < released - 2):
                raise RuntimeError("local receipt does not match this SendFile request")
            result["local_receipt"] = receipt
            size = int(receipt.get("received_size", 0))
            if size > previous_size:
                previous_size = size
                last_progress = time.monotonic()
            state = receipt.get("state")
            if state == "failed":
                raise RuntimeError(f"local receive failed: {receipt.get('error', 'invalid/incomplete file')}")
            if state == "available_in_yamcs":
                return receipt
            if receipt.get("publication_error"):
                raise RuntimeError("file saved locally; Yamcs publication failed: " + str(receipt["publication_error"]) +
                                   f"; retry publication from {path}")
            # Receiving idle is distinct from a slow HTTP publication. The
            # receiver owns its own deadline and never treats FileSent as proof.
            if state in ("receiving", "finalizing") and time.monotonic() - last_progress >= idle_timeout:
                raise TimeoutError("local file reception stopped progressing; inspect receiver receipt; no resend")
        elif time.monotonic() >= next_error_check:
            next_error_check = time.monotonic() + 2.0
            failure = downlink_error(url, instance, source, destination,
                                     datetime.fromtimestamp(released, timezone.utc))
            if failure:
                raise RuntimeError(f"F' FileDownlink reported {failure.get('type')}: {failure.get('message')}")
            if time.monotonic() - last_progress >= idle_timeout:
                raise TimeoutError("no local START receipt; inspect flight/bridge logs; no resend")
        time.sleep(0.25)
    raise TimeoutError("file delivery exceeded overall deadline; inspect retained local receipt before retrying")


@contextmanager
def helper_lock(station: Path, url: str, instance: str):
    """Portable exclusive lock, deliberately never reclaimed automatically."""
    identity = "\n".join((url.rstrip("/"), instance, "local-receiver")).encode()
    lock_dir = station / "locks"
    lock_dir.mkdir(parents=True, exist_ok=True)
    lock_path = lock_dir / ("stock-downlink-" + hashlib.sha256(identity).hexdigest()[:20] + ".lock")
    try:
        fd = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        raise RuntimeError(
            f"another helper owns {lock_path}; if it exited, recover the old sender/ground "
            "transfer before manually removing this lock") from None
    try:
        with os.fdopen(fd, "w") as output:
            json.dump({"pid": os.getpid(), "started_at": datetime.now(timezone.utc).isoformat()}, output)
        yield
    finally:
        lock_path.unlink(missing_ok=True)


def write_evidence(run_dir: Path, record: dict) -> Path:
    run_dir.mkdir(parents=True, exist_ok=True)
    path = run_dir / f"stock-downlink-{time.strftime('%Y%m%d-%H%M%S')}-{secrets.token_hex(3)}.json"
    path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    return path


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=("Issue stock SendFile once, wait for the verified local receipt, then "
                     "independently verify the fprimeFilesIn object. Native transfer rows are unused."),
        epilog=("--source-file is passed verbatim as the remote Zero W path; the ground helper never "
                "resolves or opens it. Use --expected-file only as a separate local integrity oracle. "
                "The helper does not upload files, replay commands, or treat FileSent as ground receipt."),
    )
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument("--source-file", help="remote Zero W source path, passed unchanged")
    sources.add_argument("--product-id", type=int, help="select a retained product through Yamcs and use its ProductSelected sourcePath")
    parser.add_argument("--url", default=DEFAULT_URL, help=f"Yamcs HTTP base URL (default {DEFAULT_URL})")
    parser.add_argument("--instance", default=DEFAULT_INSTANCE)
    parser.add_argument("--processor", default=DEFAULT_PROCESSOR)
    parser.add_argument("--station", type=Path,
                        help="station directory containing Yamcs-access.txt; enables saved loopback authentication")
    parser.add_argument("--receive-dir", type=Path, help="bridge spool directory (default: station/received-files)")
    parser.add_argument("--destination", help="unique receiver/bucket basename; default preserves acquisition identity")
    parser.add_argument("--expected-file", help="optional local oracle file; never sent to Yamcs")
    parser.add_argument("--expected-size", type=int, help="expected received byte count")
    parser.add_argument("--expected-sha256", help="expected received SHA-256 hex digest")
    parser.add_argument("--expected-crc16", type=parse_nonnegative_integer,
                        help="expected CRC-16/CCITT-FALSE value (decimal or 0x-prefixed hex)")
    parser.add_argument("--timeout", type=float, default=300.0, help="overall transfer timeout in seconds (default 300)")
    parser.add_argument("--idle-timeout", type=float, default=30.0,
                        help="fail helper after this many seconds without local receive progress (default 30)")
    parser.add_argument("--run-dir", help="directory for received bytes and JSON evidence; default under ignored yamcs/run")
    parser.add_argument("--json", action="store_true", help="print full receipt JSON instead of concise status")
    return parser


def main(argv: list[str] | None = None) -> int:
    global _authenticated_client
    _authenticated_client = None
    parser = build_parser()
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    if not math.isfinite(args.idle_timeout) or args.idle_timeout <= 0:
        parser.error("--idle-timeout must be finite and positive")
    if args.product_id is not None and args.product_id <= 0:
        parser.error("--product-id must be positive")
    address = parse.urlsplit(args.url)
    if (address.scheme not in ("http", "https") or not address.hostname or
            address.username is not None or address.password is not None or
            address.path not in ("", "/") or address.query or address.fragment):
        parser.error("--url must be an HTTP(S) server root without credentials, query or fragment")
    args.url = args.url.rstrip("/")
    station = (args.station or Path.home() / "yamcs-station").expanduser().resolve()

    run_dir = Path(args.run_dir).expanduser().resolve() if args.run_dir else (
        Path(__file__).resolve().parents[2] / "yamcs" / "run" /
        f"cdr-downlink-{time.strftime('%Y%m%d-%H%M%S')}-{secrets.token_hex(3)}")
    receive_dir = (args.receive_dir or station / "received-files").expanduser().resolve()
    result: dict = {
        "status": "IN_PROGRESS", "started_at": datetime.now(timezone.utc).isoformat(),
        "url": args.url, "instance": args.instance, "processor": args.processor,
        "method": "stock SendFile -> local receiver -> HTTP bucket",
        "receive_dir": str(receive_dir), "bucket": BUCKET, "network_mux": False,
        "product_id": args.product_id,
    }
    helper_started = time.monotonic()
    try:
        run_dir.mkdir(parents=True, exist_ok=True)
        if (station / "Yamcs-access.txt").is_file():
            from yamcs_client import LocalYamcs, loopback_url
            loopback_url(args.url)
            _authenticated_client = LocalYamcs(args.url, station / "Yamcs-access.txt")
        with helper_lock(station, args.url, args.instance):
            require_receiver_ready(receive_dir)
            selected = None
            if args.product_id is not None:
                selected = resolve_product(args.url, args.instance, args.processor,
                                           args.product_id, min(30.0, args.timeout))
                source = selected["source_file"]
            else:
                source = args.source_file
            source_bytes_limit(source, "source file name")
            destination = args.destination or default_destination(source, args.product_id)
            source_bytes_limit(destination, "destination file name")

            expected = None
            if args.expected_file:
                expected = Path(args.expected_file).expanduser().read_bytes()
            expectations = integrity_expectations(args, selected, expected)
            result.update({"remote_source_path": source, "destination": destination,
                           "product_selection": selected,
                           "integrity_expectations": {
                               key: value for key, value in expectations.items()
                               if key != "expected_file_bytes"
                           }})
            if existing_object(args.url, args.instance, destination):
                raise ValueError(f"Yamcs object {destination!r} already exists; use a fresh destination")

            from local_file_receiver import valid_name
            valid_name(destination)
            if any((receive_dir / (destination + suffix)).exists()
                   for suffix in ("", ".part", ".receipt.json")):
                raise ValueError("local destination already exists; use a fresh destination")
            require_receiver_ready(receive_dir)
            transfer_started = time.monotonic()
            released = time.time()
            result["request_started_at"] = datetime.now(timezone.utc).isoformat()
            # Submit exactly once. A lost HTTP response is an uncertain outcome,
            # never a reason to replay the spacecraft command automatically.
            result["command"] = issue(args.url, args.instance, args.processor,
                                      "FileHandling.fileDownlink.SendFile",
                                      {"sourceFileName": source, "destFileName": destination})
            await_local_receipt(receive_dir, source, destination, released,
                                args.timeout, args.idle_timeout, args.url, args.instance, result)
            result["transfer_completed_at"] = datetime.now(timezone.utc).isoformat()
            result["transfer_seconds"] = time.monotonic() - transfer_started
            received = api_bytes(args.url, object_endpoint(args.instance, destination))
            received_sha = hashlib.sha256(received).hexdigest()
            received_crc = binascii.crc_hqx(received, 0xFFFF)
            received_name = "received-" + destination.replace("\\", "/").rsplit("/", 1)[-1]
            received_path = run_dir / received_name
            # Exclusive creation also preserves prior evidence on integrity failure.
            with received_path.open("xb") as output:
                output.write(received)
            checks = verify_received(received, expectations)
            checks["local_file_sha256"] = received_sha == result["local_receipt"].get("sha256")
            integrity_verified = all(checks.values())
            result["received"] = {"bytes": len(received), "sha256": received_sha,
                                  "crc16_ccitt": received_crc,
                                  "integrity_verified": integrity_verified,
                                  "sha256_verified_against_source": (expectations["source_sha256_verified"] and
                                                                      checks["expected_sha256"]),
                                  "saved_path": str(received_path), "checks": checks}
            if not integrity_verified:
                raise IntegrityMismatch("independently received Yamcs bytes failed integrity checks; received bytes retained")
            result["status"] = "PASS"
    except Exception as exc:
        result["status"] = "FAIL"
        result["error"] = f"{type(exc).__name__}: {exc}"
        receipt = result.get("local_receipt", {})
        if isinstance(exc, IntegrityMismatch):
            result["failure_kind"] = "integrity_mismatch"
            result["failed_checks"] = [name for name, passed in result["received"]["checks"].items() if not passed]
            result["next_action"] = (
                "Reconcile catalog/source identity and expected size, CRC and SHA-256 with the retained local "
                "and received bytes. Preserve evidence; do not republish or resend until the mismatch is resolved.")
        elif receipt.get("state") in ("saved_locally", "available_in_yamcs"):
            from local_file_receiver import publication_retry_command
            path = receive_dir / (result["destination"] + ".receipt.json")
            result["next_action"] = publication_retry_command(path, station, args.url,
                                                             receipt.get("bucket") or BUCKET)
        elif result.get("request_started_at"):
            result["next_action"] = ("Inspect receiver/flight logs. Stop and drain the old sender with stock "
                                     "FileHandling.fileDownlink.Cancel before a fresh SendFile; no automatic resend.")
        else:
            result["next_action"] = "Correct the reported preflight problem, then request the current CSV product."
    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    result["elapsed_seconds"] = time.monotonic() - helper_started
    _authenticated_client = None
    evidence_path = write_evidence(run_dir, result)
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"{result['status']}: {result.get('destination', 'CSV request')}")
        if result.get("received"):
            if result["status"] == "PASS" and result["received"]["integrity_verified"]:
                print(f"Verified {result['received']['bytes']} bytes; SHA-256 {result['received']['sha256']}")
            else:
                print(f"Unverified {result['received']['bytes']} bytes; observed SHA-256 {result['received']['sha256']}")
        if result.get("error"):
            print(result["error"])
            print("Next: " + result["next_action"])
    print(f"Evidence: {evidence_path}")
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
