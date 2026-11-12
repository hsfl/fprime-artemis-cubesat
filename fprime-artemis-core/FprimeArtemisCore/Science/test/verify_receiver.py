#!/usr/bin/env python3
"""Exercise production C++ packets against the unchanged C3M ground receiver."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import pathlib
import struct
import subprocess
import sys
import tempfile


class CapturedSerial:
    def __init__(self) -> None:
        self.requests: list[bytes] = []

    def write(self, data: bytes) -> int:
        self.requests.append(bytes(data))
        return len(data)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=pathlib.Path)
    parser.add_argument("--receiver", required=True, type=pathlib.Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("science_legacy_payload_receiver", args.receiver)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    with tempfile.TemporaryDirectory(prefix="artemis-science-wire-") as name:
        directory = pathlib.Path(name)
        subprocess.run([str(args.executable), "--emit", name], check=True)
        source = (directory / "source.bin").read_bytes()
        packets = [bytes.fromhex(line) for line in (directory / "packets.hex").read_text().splitlines()]
        repairs = [bytes.fromhex(line) for line in (directory / "repairs.hex").read_text().splitlines()]
        events = []
        receiver = module.PayloadReceiver(
            port="portable-cache-test", baud=115200, output=directory / "ground.bin", timeout_s=30,
            output_dir=directory / "received", on_event=events.append,
        )
        serial = CapturedSerial()
        last_index = (len(source) + 34) // 35 - 1
        missing = [7, last_index]
        for packet in packets:
            if packet[2] == 2 and struct.unpack_from("<H", packet, 4)[0] in missing:
                continue
            # Feed awkward byte boundaries through the real stream parser too.
            for offset in range(0, len(packet), 3):
                receiver.rx_buffer += packet[offset:offset + 3]
                while extracted := receiver.try_extract_packet():
                    receiver.handle_packet(extracted, serial)
        assert receiver.missing_packets() == missing
        assert receiver.request_retries(serial)
        request = serial.requests[-1]
        assert request[:4] == b"N2\x04" + bytes([receiver.transfer_id])
        start = struct.unpack_from("<H", request, 4)[0]
        requested = [start + i for i in range(request[6] * 8) if request[7 + i // 8] & (1 << (i % 8))]
        assert requested == missing
        for packet in repairs:
            receiver.handle_packet(packet, serial)
        assert receiver.end_seen and receiver.missing_packets() == []
        rebuilt = receiver.reconstruct()
        assert rebuilt == source
        assert module.crc16_ccitt(rebuilt) == receiver.file_crc
        receiver.finalize_to_dir()
        saved = [event for event in events if event.kind == "transfer_saved"]
        assert len(saved) == 1 and saved[0].crc_ok is True
        assert pathlib.Path(saved[0].output_path).read_bytes() == source
        print(f"Legacy receiver interoperability: PASS ({len(source)} bytes, two locally repaired packets, "
              f"sha256={hashlib.sha256(source).hexdigest()})")


if __name__ == "__main__":
    main()
