#!/usr/bin/env python3
"""Local F′ TCP/Yamcs acceptance with a fake UART/PDU; never opens hardware.

All functional commands and received events/files use Yamcs. Source files are
only integrity oracles. A test-only TCP proxy fragments writes and can sever a
transfer; it does not add a protocol or upload anything to Yamcs storage.
"""
import argparse
import binascii
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import pty
import secrets
import select
import shutil
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from urllib import error, parse, request

import smoke

ROOT = Path(__file__).resolve().parents[2]
DEPLOYMENT = "ArtemisRpiTeensyDeployment"


def crc(data):
    return binascii.crc_hqx(data, 0xFFFF)


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class FakePdu:
    def __init__(self):
        self.master, self.slave = pty.openpty()
        self.device = os.ttyname(self.slave)
        self.stop = threading.Event()
        self.requests = []
        self.errors = []
        self.channels = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

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
                        self.errors.append("physical UART frame exceeds 220 bytes")
                        del pending[:2]
                        continue
                    if len(pending) < size + 7:
                        break
                    channel, payload = pending[2], bytes(pending[5:5 + size])
                    expected = int.from_bytes(pending[5 + size:7 + size], "little")
                    del pending[:7 + size]
                    self.channels.append(channel)
                    if crc(payload) != expected:
                        self.errors.append("physical UART CRC mismatch")
                        continue
                    if channel != 2:
                        self.errors.append(f"ground traffic leaked onto physical UART channel {channel}")
                        continue
                    if len(payload) < 13 or payload[0] != 1:
                        # RF-status requests must not be needed for this TCP profile.
                        self.errors.append("unexpected non-PDU local RPC")
                        continue
                    frame = payload[4:]
                    if frame[0:3] != b"\xa5\x02\x00" or crc(frame[1:-2]) != int.from_bytes(frame[-2:], "little"):
                        self.errors.append("invalid PDU request")
                        continue
                    opcode, sequence = frame[3], frame[4]
                    if opcode == 0:
                        answer = b"\x02"
                    elif opcode == 1:
                        answer = b"\x02\x01\x00\x00\x00\x00\x00"
                    elif opcode == 2:
                        answer = struct.pack("<HBBIB", 0x25, 0, 0, 424242, 1)
                    else:
                        self.errors.append(f"unexpected PDU opcode {opcode}")
                        continue
                    response = bytes([0xA5, 2, 1, opcode, sequence, 0, len(answer)]) + answer
                    response += crc(response[1:]).to_bytes(2, "little")
                    local = bytes([1, payload[1], 0, len(response)]) + response
                    wrapped = b"\xd4\xc3\x02" + len(local).to_bytes(2, "little") + local + crc(local).to_bytes(2, "little")
                    os.write(self.master, wrapped[:4])
                    os.write(self.master, wrapped[4:])
                    self.requests.append({"opcode": opcode, "sequence": sequence, "at": time.monotonic()})
            except OSError as exc:
                if not self.stop.is_set():
                    self.errors.append(str(exc))
                break

    def close(self):
        self.stop.set()
        self.thread.join(2)
        os.close(self.master)
        os.close(self.slave)


class FragmentingProxy:
    def __init__(self, upstream_port):
        self.upstream_port = upstream_port
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(2)
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.blocked = threading.Event()
        self.cut = threading.Event()
        self.cut_after = None
        self.forwarded = 0
        self.fragments = 0
        self.connections = 0
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            if self.blocked.is_set():
                client.close()
                continue
            upstream = None
            try:
                upstream = socket.create_connection(("127.0.0.1", self.upstream_port), timeout=2)
                upstream.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self.connections += 1
                while not self.stop.is_set() and not self.blocked.is_set():
                    for sender in select.select([client, upstream], [], [], 0.1)[0]:
                        data = sender.recv(2048)
                        if not data:
                            raise ConnectionError("peer disconnected")
                        receiver = client if sender is upstream else upstream
                        width = 73 if sender is upstream else 37
                        for offset in range(0, len(data), width):
                            if self.blocked.is_set():
                                raise ConnectionError("test interruption")
                            part = data[offset:offset + width]
                            receiver.sendall(part)
                            self.fragments += 1
                            if sender is upstream:
                                self.forwarded += len(part)
                                if self.cut_after is not None and self.forwarded >= self.cut_after:
                                    self.cut_after = None
                                    self.blocked.set()
                                    self.cut.set()
                                    raise ConnectionError("injected mid-transfer disconnect")
            except (OSError, ConnectionError):
                pass
            finally:
                client.close()
                if upstream is not None:
                    upstream.close()

    def arm_disconnect(self, bytes_before_cut=16000):
        self.cut.clear()
        self.cut_after = self.forwarded + bytes_before_cut

    def close(self):
        self.stop.set()
        self.listener.close()
        self.thread.join(4)


def terminate(child):
    if child is None or child.poll() is not None:
        return
    child.send_signal(signal.SIGINT)
    try:
        child.wait(timeout=20)
    except subprocess.TimeoutExpired:
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()


def wait_until(check, timeout=45, message="condition timed out"):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.25)
    raise RuntimeError(message)


def object_bytes(name):
    endpoint = "/api/storage/buckets/fprimeFilesIn/objects/" + parse.quote(name, safe="") + "?instance=" + smoke.INSTANCE
    try:
        with request.urlopen(smoke.URL + endpoint, timeout=5) as response:
            return response.read()
    except error.HTTPError as exc:
        if exc.code == 404:
            return None
        raise


def ping():
    token = secrets.randbelow(4294967295)
    command = smoke.issue(DEPLOYMENT + ".missionApp.PING", {"token": token})
    event = smoke.wait_for_event("Pong", after=datetime.fromisoformat(command["generationTime"].replace("Z", "+00:00")),
                                 predicate=lambda e: e.get("extra", {}).get("token") == str(token))
    return {"token": token, "command": command, "event": event}


def send_file(source, destination):
    return smoke.issue("FileHandling.fileDownlink.SendFile", {"sourceFileName": source, "destFileName": destination})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--dictionary", required=True, type=Path)
    args = parser.parse_args()
    run = ROOT / "yamcs/run" / ("cdr-acceptance-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + secrets.token_hex(3))
    run.mkdir(parents=True)
    source = run / "source"
    source.mkdir()
    fixtures = ROOT / "tools/yamcs/fixtures/payload_samples"
    fixture_manifest = json.loads((fixtures / "manifest.json").read_text())["files"]
    required_samples = {"sample-ayload-config-sohdata.csv": 729, "sample-payload-raw-science.csv": 606273}
    if set(fixture_manifest) != set(required_samples):
        raise RuntimeError("Both supplied payload fixtures are required")
    for name, byte_count in required_samples.items():
        path = fixtures / name
        data = path.read_bytes()
        expected = fixture_manifest[name]
        if len(data) != byte_count or expected["bytes"] != byte_count or hashlib.sha256(data).hexdigest() != expected["sha256"]:
            raise RuntimeError("Payload fixture identity mismatch: " + name)
        shutil.copy2(path, source / path.name)
    result = {"result": "RUNNING", "scope": "native F′ TCP + real Yamcs + fake UART/PDU only; no network mux or RF",
              "binary": str(args.binary.resolve()), "dictionary": str(args.dictionary.resolve()),
              "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              "dictionary_sha256": hashlib.sha256(args.dictionary.read_bytes()).hexdigest(), "checks": {}}
    pdu = FakePdu()
    port = free_port()
    proxy = FragmentingProxy(port)
    app = ground = None
    handles = []
    try:
        env = os.environ.copy()
        env["N2_STORAGE_ROOT"] = "onboard-storage"
        env["NEUTRON_PAYLOAD_SIM_ROOT"] = str(ROOT / "external/payload-neutron-simulation")
        for name in ("app.log", "ground.log"):
            handles.append((run / name).open("wb"))
        app = subprocess.Popen([str(args.binary.resolve()), "-a", "127.0.0.1", "-p", str(port), "-d", pdu.device], cwd=run,
                               env=env, stdout=handles[0], stderr=subprocess.STDOUT)
        # Do not connect a port-probe client: let the actual bridge own the socket.
        time.sleep(1)
        if app.poll() is not None:
            raise RuntimeError("F′ failed startup; see app.log")
        wait_until(lambda: len(pdu.requests) >= 1, 10, "Fake UART/PDU did not work with TCP client absent")
        result["checks"]["pdu_without_tcp_client"] = {"requests": len(pdu.requests), "app_running": app.poll() is None}
        ground = subprocess.Popen([str(ROOT / "tools/yamcs/run_cdr_tcp.sh"), "--dictionary", str(args.dictionary.resolve()),
                                   "--address", "127.0.0.1", "--port", str(proxy.port), "--run-dir", str(run / "ground")],
                                  cwd=ROOT, env=env, stdout=handles[1], stderr=subprocess.STDOUT)

        def ready():
            if app.poll() is not None or ground.poll() is not None:
                raise RuntimeError("F′ or ground launcher exited during startup")
            try:
                return any(i["name"] == smoke.INSTANCE and i["state"] == "RUNNING" for i in smoke.api("/api/instances").get("instances", []))
            except (OSError, error.URLError):
                return False

        wait_until(ready, 100, "Yamcs did not reach RUNNING")
        # The F Prime event subscriber starts after the instance reports RUNNING.
        # Wait for a real event from the periodic fake PDU traffic before pinging.
        smoke.wait_for_event("PduRequestHandled",
                             predicate=lambda event: event.get("type") ==
                             DEPLOYMENT + ".epsDriverArtemis.PduRequestHandled")
        result["checks"]["ping"] = ping()
        rejections = []
        for command_name, arguments in [
            ("COLLECT_AND_DOWNLINK", {"durationMinutes": 1}),
            ("VERIFIED_COLLECT_AND_DOWNLINK", {"verificationToken": 42, "durationMinutes": 1}),
        ]:
            command = smoke.issue(DEPLOYMENT + ".scienceApp." + command_name, arguments)
            released = datetime.fromisoformat(command["generationTime"].replace("Z", "+00:00"))
            rejected = smoke.wait_for_event("ScienceCommandRejected", after=released,
                                            predicate=lambda e: e.get("extra", {}).get("reason") == "3")
            opcode = rejected["extra"]["value"]
            response = smoke.wait_for_event("OpCodeError", after=released,
                                            predicate=lambda e: e.get("extra", {}).get("Opcode") == opcode
                                            and e.get("extra", {}).get("error") == "VALIDATION_ERROR")
            rejections.append({"command": command, "rejection": rejected, "response": response})
        result["checks"]["unsupported_automatic_downlink_rejected"] = rejections
        eps = smoke.issue(DEPLOYMENT + ".epsManager.REQUEST_EPS_STATUS", {})
        result["checks"]["pdu_status"] = smoke.wait_for_event("EpsStatusUpdated", after=datetime.fromisoformat(eps["generationTime"].replace("Z", "+00:00")),
                                                               predicate=lambda e: e.get("extra", {}).get("outputBitmap") == "37")
        before = len(pdu.requests)
        files = []
        for path in sorted(source.glob("*.csv")):
            name = "cdr-" + secrets.token_hex(4) + "-" + path.name
            transfer_start = time.monotonic()
            command = send_file("source/" + path.name, name)
            expected = path.read_bytes()
            next_status_request = [transfer_start]

            def poll_file():
                if time.monotonic() >= next_status_request[0]:
                    smoke.issue(DEPLOYMENT + ".epsManager.REQUEST_EPS_STATUS", {})
                    next_status_request[0] = time.monotonic() + 1
                return object_bytes(name)

            received = wait_until(poll_file, 120, "No complete stock-file object: " + name)
            transfer_end = time.monotonic()
            concurrent_rpc = [r for r in pdu.requests if transfer_start <= r["at"] <= transfer_end]
            if received != expected:
                raise RuntimeError("Independent Yamcs readback mismatch: " + name)
            (run / ("received-" + path.name)).write_bytes(received)
            files.append({"name": path.name, "command": command, "object": name, "bytes": len(received),
                          "sha256": hashlib.sha256(received).hexdigest(), "crc16_ccitt": crc(received),
                          "receipt_wait_seconds": round(transfer_end - transfer_start, 3),
                          "expected_min_data_packets_at_499_bytes": (len(received) + 498) // 499,
                          "uart_rpc_within_transfer_interval": len(concurrent_rpc)})
            if len(received) == 606273 and not concurrent_rpc:
                raise RuntimeError("No fake PDU RPC completed inside the large-file transfer interval")
        result["checks"]["sample_files"] = files
        result["checks"]["pdu_during_file_transfer"] = {"requests_before": before, "requests_after": len(pdu.requests),
                                                       "within_transfer_intervals": sum(f["uart_rpc_within_transfer_interval"] for f in files)}

        missing_name = "missing-" + secrets.token_hex(4) + ".csv"
        missing = send_file("source/does-not-exist.csv", missing_name)
        fail_event = smoke.wait_for_event("FileOpenError", after=datetime.fromisoformat(missing["generationTime"].replace("Z", "+00:00")))
        if object_bytes(missing_name) is not None:
            raise RuntimeError("Missing source falsely produced a received object")
        result["checks"]["missing_source"] = {"command": missing, "failure_event": fail_event, "object_absent": True}

        interrupted_name = "interrupted-" + secrets.token_hex(4) + ".csv"
        proxy.arm_disconnect()
        interrupted = send_file("source/sample-payload-raw-science.csv", interrupted_name)
        if not proxy.cut.wait(30):
            raise RuntimeError("Proxy did not interrupt the multi-packet transfer")
        disconnected_requests = len(pdu.requests)
        wait_until(lambda: len(pdu.requests) > disconnected_requests, 10, "UART/PDU stopped while TCP disconnected")
        # Server-side FileSent may still occur. Only independent readback is a receipt.
        if object_bytes(interrupted_name) is not None:
            raise RuntimeError("Interrupted transfer unexpectedly produced a complete object")
        result["checks"]["interrupted_transfer"] = {"command": interrupted, "object_absent": True,
                                                    "pdu_requests_while_disconnected": len(pdu.requests) - disconnected_requests}
        proxy.blocked.clear()
        # Clear the interrupted sender explicitly; never replay the old command.
        wait_until(lambda: proxy.connections >= 2, 30, "Ground did not reconnect")
        result["checks"]["reconnected_ping"] = ping()
        smoke.issue("FileHandling.fileDownlink.Cancel", {})
        time.sleep(3)
        fresh_name = "recovered-" + secrets.token_hex(4) + ".csv"
        recovered = send_file("source/sample-payload-raw-science.csv", fresh_name)
        received = wait_until(lambda: object_bytes(fresh_name), 120, "No independently received object after reconnect")
        if received != (source / "sample-payload-raw-science.csv").read_bytes():
            raise RuntimeError("Fresh transfer after reconnect has wrong bytes")
        result["checks"]["fresh_send_after_reconnect"] = {"command": recovered, "object": fresh_name, "bytes": len(received),
                                                          "sha256": hashlib.sha256(received).hexdigest()}
        helper_checks = {}
        for label, helper_args, should_succeed in [
            ("source_file", ["--source-file", "source/sample-ayload-config-sohdata.csv", "--expected-file", str(source / "sample-ayload-config-sohdata.csv")], True),
            ("missing_source", ["--source-file", "source/absent-helper.csv", "--expected-size", "1", "--expected-crc16", "0"], False),
        ]:
            helper_run = run / ("helper-" + label)
            with (run / ("helper-" + label + ".log")).open("wb") as helper_log:
                helper = subprocess.run([str(ROOT / "tools/yamcs/downlink_stock.sh"), *helper_args,
                                         "--run-dir", str(helper_run), "--timeout", "30"],
                                        cwd=ROOT, stdout=helper_log, stderr=subprocess.STDOUT, timeout=45)
            records = sorted(helper_run.glob("stock-downlink-*.json"))
            if not records or (helper.returncode == 0) != should_succeed:
                raise RuntimeError("Stock helper outcome wrong: " + label)
            helper_checks[label] = json.loads(records[-1].read_text())
        capture = smoke.issue(DEPLOYMENT + ".scienceApp.SCIENCE_CAPTURE", {"durationMinutes": 1})
        released = datetime.fromisoformat(capture["generationTime"].replace("Z", "+00:00"))
        captured = smoke.wait_for_event("CaptureComplete", after=released, timeout=90,
                                        predicate=lambda event: event.get("extra", {}).get("durationMinutes") == "1")
        if int(captured["extra"]["rows"]) != 60:
            raise RuntimeError("One-minute simulated capture did not produce 60 one-second rows")
        committed = smoke.wait_for_event("ScienceProductCommitted", after=released, timeout=90,
                                         predicate=lambda event: event.get("extra", {}).get("productId") ==
                                         captured["extra"]["productId"])
        product_id = int(committed["extra"]["productId"])
        oracle = Path(committed["extra"]["sourcePath"])
        if not oracle.is_absolute():
            oracle = run / oracle
        helper_run = run / "helper-product"
        with (run / "helper-product.log").open("wb") as helper_log:
            helper = subprocess.run([str(ROOT / "tools/yamcs/downlink_stock.sh"), "--product-id", str(product_id),
                                     "--expected-file", str(oracle), "--run-dir", str(helper_run), "--timeout", "30"],
                                    cwd=ROOT, stdout=helper_log, stderr=subprocess.STDOUT, timeout=45)
        records = sorted(helper_run.glob("stock-downlink-*.json"))
        if helper.returncode or not records:
            raise RuntimeError("Stock helper product/catalog downlink failed")
        helper_checks["retained_product"] = {"capture_command": capture, "capture_event": captured,
                                               "committed_event": committed,
                                               "stock_downlink": json.loads(records[-1].read_text())}
        result["checks"]["ground_helper"] = helper_checks
        if object_bytes(interrupted_name) is not None:
            raise RuntimeError("Interrupted transfer falsely appeared as a complete object after recovery")
        result["checks"]["interrupted_transfer"]["still_absent_after_recovery"] = True
        if pdu.errors:
            raise RuntimeError("Fake PDU errors: " + repr(pdu.errors))
        result["checks"]["physical_uart_channels"] = sorted(set(pdu.channels))
        result["checks"]["proxy"] = {"fragment_writes": proxy.fragments, "connections": proxy.connections}
        result["result"] = "PASS"
    except Exception as exc:
        result["result"] = "FAIL"
        result["error"] = str(exc)
        raise
    finally:
        terminate(ground)
        terminate(app)
        proxy.close()
        result["fake_pdu_requests"] = len(pdu.requests)
        result["fake_pdu_errors"] = pdu.errors
        pdu.close()
        for handle in handles:
            handle.close()
        result["process_exit_codes"] = {"ground": None if ground is None else ground.poll(), "app": None if app is None else app.poll()}
        (run / "acceptance.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({"result": result["result"], "error": result.get("error"), "evidence": str(run / "acceptance.json")}, indent=2), flush=True)


if __name__ == "__main__":
    main()
