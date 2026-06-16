#!/usr/bin/env python
"""Receive generic channel-1 payload blobs from the ground Teensy.

The stream is intentionally filetype-agnostic. It reconstructs bytes by packet
index, verifies CRCs, writes the blob, and sends retry bitmaps when needed.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys
import time

import serial


MAGIC = b"N2"
TYPE_HEADER = 1
TYPE_DATA = 2
TYPE_END = 3
TYPE_RETRY_REQUEST = 4
MAX_PACKET = 44
DATA_BYTES = 35


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def expected_blob_byte(product_id: int, offset: int) -> int:
    return (product_id + (offset * 31) + (offset >> 8)) & 0xFF


def expected_blob_crc(product_id: int, byte_count: int) -> int:
    crc = 0xFFFF
    for offset in range(byte_count):
        crc ^= expected_blob_byte(product_id, offset) << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


class PayloadReceiver:
    def __init__(self, port: str, baud: int, output: pathlib.Path, timeout_s: float) -> None:
        self.port = port
        self.baud = baud
        self.output = output
        self.timeout_s = timeout_s
        self.transfer_id: int | None = None
        self.product_id = 0
        self.total_bytes = 0
        self.total_packets = 0
        self.file_crc = 0
        self.packet_data_bytes = DATA_BYTES
        self.packets: dict[int, bytes] = {}
        self.end_seen = False

    def run(self) -> int:
        deadline = time.monotonic() + self.timeout_s
        with serial.Serial(self.port, self.baud, timeout=0.2) as ser:
            while time.monotonic() < deadline:
                packet = self.read_packet(ser)
                if not packet:
                    if self.end_seen and self.complete:
                        break
                    continue
                self.handle_packet(packet, ser)
                if self.end_seen and self.complete:
                    break

            if not self.complete:
                self.request_retries(ser)
                retry_deadline = time.monotonic() + min(20.0, self.timeout_s)
                while time.monotonic() < retry_deadline and not self.complete:
                    packet = self.read_packet(ser)
                    if packet:
                        self.handle_packet(packet, ser)

        if not self.complete:
            missing = self.missing_packets()
            print(f"incomplete: received={len(self.packets)} total={self.total_packets} missing={len(missing)}")
            return 2

        blob = self.reconstruct()
        actual_crc = crc16_ccitt(blob)
        if actual_crc != self.file_crc:
            print(f"crc mismatch: actual=0x{actual_crc:04x} expected=0x{self.file_crc:04x}")
            return 3

        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.output.write_bytes(blob)
        print(
            "complete: "
            f"product={self.product_id} transfer={self.transfer_id} bytes={len(blob)} "
            f"packets={self.total_packets} crc=0x{actual_crc:04x} output={self.output}"
        )
        return 0

    @property
    def complete(self) -> bool:
        return self.total_packets > 0 and len(self.packets) == self.total_packets

    def read_packet(self, ser: serial.Serial) -> bytes:
        packet = bytearray(ser.read(MAX_PACKET))
        if not packet:
            return b""
        partial_deadline = time.monotonic() + 0.5
        while len(packet) < MAX_PACKET and time.monotonic() < partial_deadline:
            chunk = ser.read(MAX_PACKET - len(packet))
            if chunk:
                packet += chunk
        if len(packet) != MAX_PACKET:
            return b""
        return bytes(packet)

    def handle_packet(self, packet: bytes, ser: serial.Serial) -> None:
        if len(packet) < 4 or packet[0:2] != MAGIC:
            return
        packet_type = packet[2]
        transfer_id = packet[3]

        if packet_type == TYPE_HEADER:
            self.handle_header(packet)
            return

        if self.transfer_id is None or transfer_id != self.transfer_id:
            return

        if packet_type == TYPE_DATA:
            self.handle_data(packet)
        elif packet_type == TYPE_END:
            self.end_seen = True
            if not self.complete:
                self.request_retries(ser)

    def handle_header(self, packet: bytes) -> None:
        if len(packet) < 17:
            return
        self.transfer_id = packet[3]
        self.product_id = struct.unpack_from("<I", packet, 4)[0]
        self.total_bytes = struct.unpack_from("<I", packet, 8)[0]
        self.total_packets = struct.unpack_from("<H", packet, 12)[0]
        self.packet_data_bytes = packet[14]
        self.file_crc = struct.unpack_from("<H", packet, 15)[0]
        self.packets.clear()
        self.end_seen = False
        expected_crc = expected_blob_crc(self.product_id, self.total_bytes)
        print(
            "header: "
            f"product={self.product_id} transfer={self.transfer_id} bytes={self.total_bytes} "
            f"packets={self.total_packets} crc=0x{self.file_crc:04x} "
            f"pattern_crc=0x{expected_crc:04x}"
        )

    def handle_data(self, packet: bytes) -> None:
        if len(packet) < 9:
            return
        index = struct.unpack_from("<H", packet, 4)[0]
        valid_len = packet[6]
        crc_offset = 7 + valid_len
        if valid_len > self.packet_data_bytes or crc_offset + 2 > len(packet):
            return
        expected_crc = struct.unpack_from("<H", packet, crc_offset)[0]
        if crc16_ccitt(packet[:crc_offset]) != expected_crc:
            return
        if index < self.total_packets:
            self.packets[index] = packet[7:crc_offset]
            if len(self.packets) % 50 == 0 or len(self.packets) == self.total_packets:
                print(f"progress: {len(self.packets)}/{self.total_packets}")

    def request_retries(self, ser: serial.Serial) -> None:
        if self.transfer_id is None or self.total_packets == 0:
            return
        missing = self.missing_packets()
        if not missing:
            return
        start = missing[0]
        span = [idx for idx in missing if idx < start + 8 * 36]
        bitmap = bytearray(36)
        for idx in span:
            rel = idx - start
            bitmap[rel // 8] |= 1 << (rel % 8)
        while bitmap and bitmap[-1] == 0:
            bitmap.pop()
        request = bytearray()
        request += MAGIC
        request += bytes([TYPE_RETRY_REQUEST, self.transfer_id])
        request += struct.pack("<H", start)
        request += bytes([len(bitmap)])
        request += bitmap
        request += bytes(MAX_PACKET - len(request))
        ser.write(bytes(request))
        print(f"retry: start={start} count={len(span)} bitmap_bytes={len(bitmap)}")

    def missing_packets(self) -> list[int]:
        return [idx for idx in range(self.total_packets) if idx not in self.packets]

    def reconstruct(self) -> bytes:
        chunks = [self.packets[idx] for idx in range(self.total_packets)]
        return b"".join(chunks)[: self.total_bytes]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Payload serial port, usually ground Teensy SerialUSB2")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("payload_blob.bin"))
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args(argv)
    return PayloadReceiver(args.port, args.baud, args.output, args.timeout).run()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
