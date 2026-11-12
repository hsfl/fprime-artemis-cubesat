"""Archive actual received FDP files and verify their Yamcs object readback."""
from __future__ import annotations

from collections import OrderedDict
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import queue
import struct
import threading

from common import crc16, log_event

MAX_BYTES = 196608
BUCKET = "artemis-science"


def validate_received(blob: bytes, manifest: dict) -> None:
    if not manifest.get("crc_ok") or manifest.get("partial"):
        raise ValueError("Only complete CRC-valid ground products may be archived")
    if not 1 <= len(blob) <= MAX_BYTES or len(blob) != manifest["bytes"]:
        raise ValueError("Ground product length mismatch or outside cache limit")
    if crc16(blob) != manifest["crc16"] or hashlib.sha256(blob).hexdigest() != manifest["sha256"]:
        raise ValueError("Ground product bytes changed")
    if len(blob) < 6 or struct.unpack_from(">H", blob)[0] != 5:
        raise ValueError("Input is not an F Prime data product")



class Publisher:
    """Bounded worker/outbox; publication never blocks UART/RF processing."""

    def __init__(self, directory: Path, dictionary: Path, dictionary_sha: str, run_id: str,
                 url: str = "http://127.0.0.1:8090") -> None:
        self.directory, self.dictionary, self.dictionary_sha = directory, dictionary, dictionary_sha
        self.run_id, self.url = run_id, url
        self.jobs: queue.Queue = queue.Queue(maxsize=2)
        self.pending: set[str] = set()
        self.done: OrderedDict[str, None] = OrderedDict()
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.worker, name="science-yamcs-publisher", daemon=True)
        (directory / "archived").mkdir(parents=True, exist_ok=True)
        (directory / "outbox").mkdir(exist_ok=True)
        self.thread.start()

    def submit(self, blob: bytes, manifest: dict) -> bool:
        validate_received(blob, manifest)
        key = f"{manifest['product_id']:08x}-{manifest['transfer_id']:02x}-{manifest['sha256'][:12]}"
        with self.lock:
            if key in self.pending or key in self.done:
                return True
            try:
                self.jobs.put_nowait((key, bytes(blob), dict(manifest)))
            except queue.Full:
                return False
            self.pending.add(key)
        return True

    def worker(self) -> None:
        while not self.stop.is_set():
            try:
                key, blob, manifest = self.jobs.get(timeout=0.1)
            except queue.Empty:
                continue
            outbox = self.directory / "outbox" / (key + ".json")
            success = False
            last_error = None
            for attempt in range(30):
                if self.stop.is_set():
                    break
                try:
                    result = self.publish(key, blob, manifest)
                    outbox.write_text(json.dumps({"state": "verified", **result}, indent=2) + "\n")
                    log_event(self.directory, "yamcs_science_verified", **result)
                    success = True
                    break
                except Exception as exc:
                    last_error = str(exc)
                    detail = {"state": "pending", "attempt": attempt + 1, "error": str(exc), **manifest}
                    outbox.write_text(json.dumps(detail, indent=2) + "\n")
                    log_event(self.directory, "yamcs_science_retry", attempt=attempt + 1, detail=str(exc))
                    self.stop.wait(min(5, 0.5 * (attempt + 1)))
            if not success:
                outbox.write_text(json.dumps({"state": "stopped" if self.stop.is_set() else "failed",
                                              "error": last_error, **manifest}, indent=2) + "\n")
            with self.lock:
                self.pending.discard(key)
                if success:
                    self.done[key] = None
                    while len(self.done) > 64:
                        self.done.popitem(last=False)
            self.jobs.task_done()

    def publish(self, key: str, blob: bytes, manifest: dict) -> dict:
        validate_received(blob, manifest)
        from yamcs.client import YamcsClient
        client = YamcsClient(self.url)
        original = client.ctx.session.request
        def bounded(method, url, **kwargs):
            kwargs.setdefault("timeout", 5)
            return original(method, url, **kwargs)
        client.ctx.session.request = bounded
        try:
            storage = client.get_storage_client()
            if not any(bucket.name == BUCKET for bucket in storage.list_buckets()):
                storage.create_bucket(BUCKET)
            name = f"lepton-{self.run_id}-{key}.fdp"
            metadata = {"runId": self.run_id, "productId": str(manifest["product_id"]),
                        "transferId": str(manifest["transfer_id"]), "bytes": str(len(blob)),
                        "crc16Ccitt": str(manifest["crc16"]), "sha256": manifest["sha256"],
                        "producerDictionarySha256": self.dictionary_sha}
            storage.upload_object(BUCKET, name, io.BytesIO(blob), content_type="application/octet-stream", metadata=metadata)
            received = storage.download_object(BUCKET, name)
            if received != blob or hashlib.sha256(received).hexdigest() != manifest["sha256"]:
                raise ValueError("Yamcs object readback differs from ground-received bytes")
            archive = self.directory / "archived"
            archived = archive / name
            archived.write_bytes(received)
            receipt = {**manifest, "run_id": self.run_id, "bucket": BUCKET, "object": name,
                       "dictionary_sha256": self.dictionary_sha, "verified_ground_receipt": True,
                       "verified_at": datetime.now(timezone.utc).isoformat(),
                       "download_path": "/api/storage/buckets/" + BUCKET + "/objects/" + name,
                       "local_ground_file": "archived/" + name}
            receipt_bytes = (json.dumps(receipt, sort_keys=True, indent=2) + "\n").encode()
            receipt_name = name + ".receipt.json"
            storage.upload_object(BUCKET, receipt_name, io.BytesIO(receipt_bytes), content_type="application/json", metadata=metadata)
            if storage.download_object(BUCKET, receipt_name) != receipt_bytes:
                raise ValueError("Yamcs receipt readback mismatch")
            (archive / receipt_name).write_bytes(receipt_bytes)
            client.send_event("fprime-project", "Ground FDP file archived and independently verified",
                              event_type="GroundScienceReceiptVerified", source="artemis-twin-science",
                              severity="info", extra={**metadata, "objectName": name, "verifiedGroundReceipt": "true"})
            return receipt
        finally:
            client.close()

    def close(self) -> None:
        self.stop.set()
        self.thread.join(timeout=1)
