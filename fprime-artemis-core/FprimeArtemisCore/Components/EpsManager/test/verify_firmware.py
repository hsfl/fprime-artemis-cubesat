#!/usr/bin/env python3
"""Production client frames/replies against actual PDU C firmware, no hardware."""
from __future__ import annotations

import argparse
import os
import pathlib
import re
import selectors
import struct
import subprocess
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--host", type=pathlib.Path, required=True)
    parser.add_argument("--header", type=pathlib.Path, required=True)
    args = parser.parse_args()
    text = args.header.read_text()

    def constant(name: str) -> int:
        match = re.search(rf"\b{re.escape(name)}\s*(?:=\s*|\s+)(0x[0-9A-Fa-f]+|[0-9]+)U?\b", text)
        assert match is not None, name
        return int(match[1], 0)

    child = subprocess.Popen([str(args.host)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
    assert child.stdin is not None and child.stdout is not None
    poll = selectors.DefaultSelector()
    poll.register(child.stdout, selectors.EVENT_READ)

    def receive(timeout: float = 1.0) -> bytes:
        received = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if poll.select(max(0, deadline - time.monotonic())):
                received.extend(os.read(child.stdout.fileno(), 128))
                if len(received) >= 7 and len(received) == 9 + received[6]:
                    return bytes(received)
        raise TimeoutError("PDU did not emit a complete framed response")

    def encoded(opcode: str, payload: bytes = b"") -> bytes:
        return bytes.fromhex(subprocess.check_output([str(args.probe), "--wire", str(constant(opcode)), payload.hex()], text=True).strip())

    def request(opcode: str, payload: bytes = b"") -> tuple[int, bytes]:
        child.stdin.write(encoded(opcode, payload))
        response = receive()
        output = subprocess.check_output([str(args.probe), "--wire", str(constant(opcode)), payload.hex(), response.hex()], text=True).splitlines()
        return int(output[0]), bytes.fromhex(output[1]) if len(output) > 1 else b""

    ok = constant("PDU_V2_STATUS_OK")
    output_id = constant("PDU_OUTPUT_3V3_1")
    try:
        status, payload = request("PDU_V2_OP_PING")
        assert status == ok and payload == bytes([constant("PDU_V2_VERSION")])
        status, payload = request("PDU_V2_OP_GET_SUMMARY_STATUS")
        assert status == ok and len(payload) == constant("PDU_V2_SUMMARY_STATUS_LEN")
        assert struct.unpack_from("<H", payload)[0] == 0
        status, payload = request("PDU_V2_OP_SET_OUTPUT_STATE", bytes([output_id, 1]))
        assert status == ok and payload == bytes([output_id, 1])
        status, payload = request("PDU_V2_OP_GET_SUMMARY_STATUS")
        assert status == ok and struct.unpack_from("<H", payload)[0] & 1
        status, _ = request("PDU_V2_OP_SET_OUTPUT_STATE", bytes([constant("PDU_OUTPUT_BURN1"), 1]))
        assert status == constant("PDU_V2_STATUS_BAD_PARAM")

        status, payload = request("PDU_V2_OP_POWER_CYCLE_OUTPUT", bytes([output_id]) + struct.pack("<H", 200))
        assert status == ok and payload == bytes([output_id, 0])
        status, _ = request("PDU_V2_OP_SET_OUTPUT_STATE", bytes([constant("PDU_OUTPUT_3V3_2"), 1]))
        assert status == constant("PDU_V2_STATUS_BUSY")
        time.sleep(0.3)
        status, payload = request("PDU_V2_OP_GET_OUTPUT_STATE", bytes([output_id]))
        assert status == ok and payload == bytes([output_id, 1])

        bad = bytearray(encoded("PDU_V2_OP_PING")); bad[-1] ^= 1
        child.stdin.write(bad)
        try:
            receive(0.05)
            raise AssertionError("bad-CRC request was executed")
        except TimeoutError:
            pass
        assert request("PDU_V2_OP_PING")[0] == ok

        # Exercise parser gap recovery without changing firmware receive code.
        child.stdin.write(encoded("PDU_V2_OP_PING")[:4])
        time.sleep(0.15)
        assert request("PDU_V2_OP_PING")[0] == ok

        status, _ = request("PDU_V2_OP_SOFTWARE_RESET")
        assert status == ok
        time.sleep(1.2)
        assert request("PDU_V2_OP_PING")[0] == ok
        status, payload = request("PDU_V2_OP_GET_SUMMARY_STATUS")
        assert status == ok and payload[2] == 0x20 and struct.unpack_from("<H", payload)[0] == 0
        print("Actual PDU firmware interoperability: PASS (readback, timed restore, BUSY, CRC/gap recovery, modeled reset)")
    finally:
        poll.close()
        child.terminate()
        child.wait(timeout=3)


if __name__ == "__main__":
    main()
