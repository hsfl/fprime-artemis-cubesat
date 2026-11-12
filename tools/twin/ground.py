#!/usr/bin/env python3
"""Ground RF bridge, production science receiver and Yamcs raw FDP archive."""
from __future__ import annotations

import asyncio
import contextlib
from dataclasses import asdict
import hashlib
import importlib
import json
import os
from pathlib import Path
import queue
import secrets
import shutil
import signal
import sys
import threading
import time

import yaml

from common import RadioPacket, RadioParser, log_event
from radio_ground import GroundLink
from science_ground import Publisher
from artifacts import verify

ROOT = Path(__file__).resolve().parents[2]
DATA = Path("/data")
sys.path.insert(0, str(ROOT / "ArtemisRpiTeensy_N2/tools"))
from payload_receiver import PayloadReceiver


class RetryWriter:
    def __init__(self, queue: asyncio.Queue) -> None:
        self.queue = queue
        self.loop = None

    def write(self, data: bytes) -> int:
        if self.loop is None:
            raise RuntimeError("Ground retry event loop is not ready")
        # Called from the bounded science receiver worker, never the radio loop.
        future = asyncio.run_coroutine_threadsafe(self.queue.put((1, bytes(data))), self.loop)
        future.result(timeout=2)
        return len(data)


def verify_artifacts() -> tuple[dict, dict]:
    results = []
    for role in ("fc", "pi"):
        base = Path("/artifacts") / role
        results.append(verify(role, base))
    if results[0]["framework_sha"] != results[1]["framework_sha"]:
        raise RuntimeError("Split deployments use different F Prime revisions")
    if not results[0].get("working_source_sha256") or results[0]["working_source_sha256"] != results[1].get("working_source_sha256"):
        raise RuntimeError("Split deployment artifacts come from different working source trees; rebuild both roles")
    if results[0].get("dependency_states") != results[1].get("dependency_states"):
        raise RuntimeError("Split deployment artifacts use different dependency working states")
    if not results[0].get("pdu_dependency") or results[0]["pdu_dependency"] != results[1].get("pdu_dependency"):
        raise RuntimeError("Split deployment artifacts use different or missing PDU source identities")
    return results[0], results[1]


class Ground:
    def __init__(self) -> None:
        self.stop = asyncio.Event()
        self.exit_code = 0
        self.fc_metadata, self.pi_metadata = verify_artifacts()
        DATA.mkdir(parents=True, exist_ok=True)
        (DATA / "dictionaries").mkdir(exist_ok=True)
        for role in ("fc", "pi"):
            shutil.copy2(Path("/artifacts") / role / "dict/topology.json", DATA / "dictionaries" / (role + ".json"))
            shutil.copy2(Path("/artifacts") / role / "manifest.json", DATA / "dictionaries" / (role + ".manifest.json"))
        self.writer: asyncio.StreamWriter | None = None
        self.yamcs_writer: asyncio.StreamWriter | None = None
        self.queue: asyncio.Queue = asyncio.Queue(maxsize=128)
        self.radio = GroundLink(ROOT, self.send_packet, self.frame)
        self.publisher = Publisher(DATA, DATA / "dictionaries/pi.json", self.pi_metadata["dictionary_sha256"],
                                   os.environ.get("TWIN_RUN_ID", "local"))
        self.receiver = PayloadReceiver("virtual-radio-channel1", 115200, DATA / "received.fdp", 30,
                                        output_dir=DATA / "received", ext=".fdp", on_event=self.science_event,
                                        transfer_timeout_s=25, absolute_transfer_timeout_s=120,
                                        save_partial_on_timeout=True)
        self.retry = RetryWriter(self.queue)
        self.science_queue = queue.Queue(maxsize=256)
        self.science_stop = threading.Event()
        self.science_thread = threading.Thread(target=self.science_worker, name="science-radio-receiver", daemon=True)
        self.yamcs: asyncio.subprocess.Process | None = None

    async def send_packet(self, packet: RadioPacket) -> None:
        if self.writer is None:
            raise ConnectionError("Virtual ground radio is disconnected")
        self.writer.write(packet.encode())
        await asyncio.wait_for(self.writer.drain(), 2)

    async def frame(self, channel: int, data: bytes) -> None:
        if channel == 0:
            if self.yamcs_writer is not None:
                self.yamcs_writer.write(data)
                await self.yamcs_writer.drain()
            return
        try:
            self.science_queue.put_nowait(bytes(data))
        except queue.Full:
            # Channel-1 application repair can recover this explicit drop.
            log_event(DATA, "science_receiver_backpressure", dropped_bytes=len(data))

    def science_worker(self) -> None:
        while not self.science_stop.is_set():
            try:
                try:
                    data = self.science_queue.get(timeout=0.05)
                except queue.Empty:
                    data = None
                if data is not None:
                    try:
                        self.receiver.handle_packet(data, self.retry)
                        if self.receiver.complete:
                            self.receiver.finalize_to_dir()
                            self.receiver.reset_transfer()
                        elif self.receiver.end_seen:
                            self.receiver.request_retries_if_due(self.retry, force=True)
                    finally:
                        self.science_queue.task_done()
                self.receiver.request_retries_if_due(self.retry)
                reason = self.receiver.transfer_expiry_reason()
                if reason is not None:
                    self.receiver.finalize_partial_to_dir(reason)
                    self.receiver.reset_transfer()
            except Exception as exc:
                log_event(DATA, "science_receiver_error", detail=str(exc))
                self.receiver.reset_transfer()
        if self.receiver.transfer_id is not None and not self.receiver.complete:
            self.receiver.finalize_partial_to_dir("ground service stopped", completion_reason="shutdown")

    def science_event(self, event) -> None:
        record = asdict(event)
        record.pop("preview_pixels", None)
        if event.kind != "progress" or event.received_packets % 100 == 0:
            log_event(DATA, "science_receiver", **record)
        if event.kind != "transfer_saved" or event.crc_ok is not True or event.partial:
            return
        path = Path(event.output_path)
        blob = path.read_bytes()
        manifest = {"product_id": event.product_id, "transfer_id": event.transfer_id,
                    "bytes": len(blob), "crc16": event.actual_crc, "sha256": hashlib.sha256(blob).hexdigest(),
                    "crc_ok": True, "partial": False, "retry_rounds": event.retry_rounds,
                    "received_file": path.name}
        if not self.publisher.submit(blob, manifest):
            log_event(DATA, "science_publication_backpressure", product_id=event.product_id)

    async def radio_connection(self) -> None:
        host = os.environ.get("TWIN_DEVICES_HOST", "devices")
        while not self.stop.is_set():
            try:
                reader, self.writer = await asyncio.open_connection(host, 6107)
                parser = RadioParser()
                while data := await reader.read(4096):
                    for packet in parser.feed(data):
                        await self.radio.receive(packet)
            except (OSError, ConnectionError, asyncio.TimeoutError) as exc:
                log_event(DATA, "ground_radio_disconnected", detail=str(exc))
            finally:
                if self.writer is not None:
                    self.writer.close()
                self.writer = None
            await asyncio.sleep(0.25)

    async def transmission(self) -> None:
        while not self.stop.is_set():
            channel, data = await self.queue.get()
            try:
                if not await self.radio.transmit(channel, data):
                    log_event(DATA, "ground_uplink_failed", channel=channel)
            except (OSError, ConnectionError, asyncio.TimeoutError) as exc:
                log_event(DATA, "ground_uplink_failed", channel=channel, detail=str(exc))
            finally:
                self.queue.task_done()

    async def yamcs_tcp(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        if self.yamcs_writer is not None:
            writer.close()
            return
        self.yamcs_writer = writer
        try:
            while data := await reader.read(220):
                await self.queue.put((0, data))
        finally:
            self.yamcs_writer = None
            writer.close()

    async def start_yamcs(self) -> None:
        import fprime_yamcs
        package = Path(next(iter(fprime_yamcs.__path__)))
        config = DATA / "yamcs-config"
        shutil.copytree(package / "yamcs/src/main/yamcs", config, dirs_exist_ok=True)
        global_path = config / "etc/yamcs.yaml"
        settings = yaml.safe_load(global_path.read_text())
        key = Path("/yamcs-data/local-signing-key")
        key.parent.mkdir(parents=True, exist_ok=True)
        if not key.exists():
            key.write_text(secrets.token_hex(32))
            key.chmod(0o600)
        settings["secretKey"] = key.read_text().strip()
        for service in settings["services"]:
            if service["class"].endswith("HttpServer"):
                service["args"].update(address="0.0.0.0", port=8090)
                service["args"].pop("cors", None)
        global_path.write_text(yaml.safe_dump(settings))
        env = os.environ.copy()
        env["FPRIME_DOWNLINK_DIR"] = str(DATA / "stock-file-diagnostics")
        command = ["fprime-yamcs", "--no-app", "--dictionary", str(DATA / "dictionaries/fc.json"),
                   "--communication-selection", "tcp-fast-client", "--tcp-fast-address", "127.0.0.1",
                   "--tcp-fast-port", "5100",
                   "--yamcs-config-dir", str(config), "--yamcs-data-dir", "/yamcs-data",
                   "--logs", str(DATA / "yamcs-logs"), "--log-directly", "--log-to-stdout", "--skip-browser-open"]
        self.yamcs = await asyncio.create_subprocess_exec(*command, cwd=DATA, env=env,
                                                        stdout=asyncio.subprocess.PIPE,
                                                        stderr=asyncio.subprocess.STDOUT,
                                                        start_new_session=True)
        self.yamcs_log_task = asyncio.create_task(self.yamcs_log())

    async def stop_yamcs(self) -> None:
        if self.yamcs is not None and self.yamcs.returncode is None:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(self.yamcs.pid, signal.SIGTERM)
            try:
                await asyncio.wait_for(self.yamcs.wait(), 8)
            except asyncio.TimeoutError:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(self.yamcs.pid, signal.SIGKILL)
                await self.yamcs.wait()
        task = getattr(self, "yamcs_log_task", None)
        if task is not None:
            try:
                await asyncio.wait_for(asyncio.shield(task), 3)
            except asyncio.TimeoutError:
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)

    async def yamcs_log(self) -> None:
        assert self.yamcs is not None and self.yamcs.stdout is not None
        with (DATA / "yamcs.log").open("ab") as stream:
            while line := await self.yamcs.stdout.readline():
                stream.write(line)
                stream.flush()
                print(line.decode(errors="replace"), end="", flush=True)
        if not self.stop.is_set():
            self.exit_code = await self.yamcs.wait()
            log_event(DATA, "yamcs_exited", returncode=self.exit_code)
            self.stop.set()

    async def run(self) -> int:
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, self.stop.set)
        server = await asyncio.start_server(self.yamcs_tcp, "127.0.0.1", 5100)
        self.retry.loop = loop
        self.science_thread.start()
        tasks = [asyncio.create_task(self.radio_connection()), asyncio.create_task(self.transmission())]
        try:
            await self.start_yamcs()
            log_event(DATA, "ground_ready", yamcs="http://127.0.0.1:8090", archive_bucket="artemis-science")
            await self.stop.wait()
        finally:
            self.science_stop.set()
            await asyncio.to_thread(self.science_thread.join, 3)
            self.publisher.close()
            await self.stop_yamcs()
            server.close()
            await server.wait_closed()
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
        return self.exit_code


if __name__ == "__main__":
    raise SystemExit(asyncio.run(Ground().run()))
