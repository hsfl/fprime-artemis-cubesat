#!/usr/bin/env python
"""Receive generic channel-1 payload blobs from the ground Teensy.

The stream is intentionally filetype-agnostic. It reconstructs bytes by packet
index, verifies CRCs, writes the blob, and sends retry bitmaps when needed.

Two modes:
  * single-file (default): wait for one transfer and write it to --output.
  * directory (--output-dir DIR): keep listening and save every completed
    transfer into DIR as a uniquely named file for background HIL capture.
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
RETRY_INTERVAL_S = 5.0
RETRY_AFTER_SILENCE_S = 8.0


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


class PayloadReceiver:
    def __init__(
        self,
        port: str,
        baud: int,
        output: pathlib.Path,
        timeout_s: float,
        output_dir: pathlib.Path | None = None,
        ext: str = ".bin",
        debug: bool = False,
    ) -> None:
        self.port = port
        self.baud = baud
        self.output = output
        self.output_dir = output_dir
        self.ext = ext
        self.debug = debug
        self.debug_total_bytes = 0
        self.debug_last_report_s = 0.0
        self.debug_seen_magic = False
        self.debug_type_counts: dict[int, int] = {}
        self.received_count = 0
        self.timeout_s = timeout_s
        self.transfer_id: int | None = None
        self.product_id = 0
        self.total_bytes = 0
        self.total_packets = 0
        self.file_crc = 0
        self.packet_data_bytes = DATA_BYTES
        self.packets: dict[int, bytes] = {}
        self.end_seen = False
        self.rx_buffer = bytearray()
        self.next_retry_request_s = 0.0
        self.last_packet_s = 0.0

    def run(self) -> int:
        deadline = time.monotonic() + self.timeout_s
        with serial.Serial(self.port, self.baud, timeout=0.2) as ser:
            while time.monotonic() < deadline:
                packet = self.read_packet(ser)
                if not packet:
                    self.request_retries_if_due(ser)
                    if self.complete:
                        break
                    continue
                self.handle_packet(packet, ser)
                self.request_retries_if_due(ser)
                if self.complete:
                    break

            if not self.complete:
                self.request_retries_if_due(ser, force=True)
                retry_deadline = time.monotonic() + min(20.0, self.timeout_s)
                while time.monotonic() < retry_deadline and not self.complete:
                    packet = self.read_packet(ser)
                    if packet:
                        self.handle_packet(packet, ser)
                    self.request_retries_if_due(ser)

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
        packet = self.try_extract_packet()
        if packet:
            return packet

        partial_deadline = time.monotonic() + 0.5
        while time.monotonic() < partial_deadline:
            chunk = ser.read(MAX_PACKET)
            if chunk:
                self.debug_report(chunk)
                self.rx_buffer += chunk
                packet = self.try_extract_packet()
                if packet:
                    return packet
            elif not self.rx_buffer:
                return b""
        return b""

    def debug_report(self, chunk: bytes) -> None:
        if not self.debug:
            return
        self.debug_total_bytes += len(chunk)
        if not self.debug_seen_magic and MAGIC in (self.rx_buffer[-1:] + chunk):
            self.debug_seen_magic = True
            index = chunk.find(MAGIC)
            snippet = chunk[index : index + 8] if index >= 0 else chunk[:8]
            print(f"debug: first N2 magic seen; bytes={snippet.hex(' ')}")
        now = time.monotonic()
        if now - self.debug_last_report_s >= 1.0:
            self.debug_last_report_s = now
            counts = self.debug_type_counts
            other = sum(
                value
                for key, value in counts.items()
                if key not in (TYPE_HEADER, TYPE_DATA, TYPE_END)
            )
            print(
                f"debug: rx_bytes_total={self.debug_total_bytes} "
                f"types[header={counts.get(TYPE_HEADER, 0)} data={counts.get(TYPE_DATA, 0)} "
                f"end={counts.get(TYPE_END, 0)} other={other}] "
                f"header_seen={self.transfer_id is not None} "
                f"packets={len(self.packets)}/{self.total_packets}"
            )

    def try_extract_packet(self) -> bytes:
        while True:
            magic_index = self.rx_buffer.find(MAGIC)
            if magic_index < 0:
                self.rx_buffer.clear()
                return b""
            if magic_index > 0:
                del self.rx_buffer[:magic_index]
            if len(self.rx_buffer) < 4:
                return b""

            packet_type = self.rx_buffer[2]
            if packet_type == TYPE_HEADER:
                needed = 17
            elif packet_type == TYPE_END:
                needed = 8
            elif packet_type == TYPE_DATA:
                if len(self.rx_buffer) < 7:
                    return b""
                valid_len = self.rx_buffer[6]
                if valid_len > self.packet_data_bytes:
                    del self.rx_buffer[0]
                    continue
                needed = 7 + valid_len + 2
            elif packet_type == TYPE_RETRY_REQUEST:
                if len(self.rx_buffer) < 7:
                    return b""
                needed = 7 + self.rx_buffer[6]
            else:
                del self.rx_buffer[0]
                continue

            if needed > MAX_PACKET:
                del self.rx_buffer[0]
                continue
            if len(self.rx_buffer) < needed:
                return b""
            packet = bytes(self.rx_buffer[:needed])
            del self.rx_buffer[:needed]
            return packet

    def handle_packet(self, packet: bytes, ser: serial.Serial) -> None:
        if len(packet) < 4 or packet[0:2] != MAGIC:
            return
        packet_type = packet[2]
        transfer_id = packet[3]

        if self.debug:
            self.debug_type_counts[packet_type] = self.debug_type_counts.get(packet_type, 0) + 1

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
                self.request_retries_if_due(ser, force=True)

    def request_retries_if_due(self, ser: serial.Serial, force: bool = False) -> None:
        now = time.monotonic()
        header_seen = self.transfer_id is not None and self.total_packets > 0
        if not self.end_seen and (
            not header_seen or self.last_packet_s == 0.0 or now - self.last_packet_s < RETRY_AFTER_SILENCE_S
        ):
            return
        if not force and now < self.next_retry_request_s:
            return
        if self.request_retries(ser):
            self.next_retry_request_s = now + RETRY_INTERVAL_S

    def handle_header(self, packet: bytes) -> None:
        if len(packet) < 17:
            return
        transfer_id = packet[3]
        product_id = struct.unpack_from("<I", packet, 4)[0]
        total_bytes = struct.unpack_from("<I", packet, 8)[0]
        total_packets = struct.unpack_from("<H", packet, 12)[0]
        packet_data_bytes = packet[14]
        file_crc = struct.unpack_from("<H", packet, 15)[0]
        same_transfer = (
            self.transfer_id == transfer_id
            and self.product_id == product_id
            and self.total_bytes == total_bytes
            and self.total_packets == total_packets
            and self.packet_data_bytes == packet_data_bytes
            and self.file_crc == file_crc
        )
        if same_transfer:
            self.last_packet_s = time.monotonic()
            return

        self.transfer_id = transfer_id
        self.product_id = product_id
        self.total_bytes = total_bytes
        self.total_packets = total_packets
        self.packet_data_bytes = packet_data_bytes
        self.file_crc = file_crc
        self.packets.clear()
        self.end_seen = False
        self.last_packet_s = time.monotonic()
        print(
            "header: "
            f"product={self.product_id} transfer={self.transfer_id} bytes={self.total_bytes} "
            f"packets={self.total_packets} crc=0x{self.file_crc:04x}"
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
            self.last_packet_s = time.monotonic()
            if len(self.packets) % 50 == 0 or len(self.packets) == self.total_packets:
                print(f"progress: {len(self.packets)}/{self.total_packets}")

    def request_retries(self, ser: serial.Serial) -> bool:
        if self.transfer_id is None or self.total_packets == 0:
            return False
        missing = self.missing_packets()
        if not missing:
            return False
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
        ser.write(bytes(request))
        print(f"retry: start={start} count={len(span)} bitmap_bytes={len(bitmap)}")
        return True

    def missing_packets(self) -> list[int]:
        return [idx for idx in range(self.total_packets) if idx not in self.packets]

    def reconstruct(self) -> bytes:
        chunks = [self.packets[idx] for idx in range(self.total_packets)]
        return b"".join(chunks)[: self.total_bytes]

    def reset_transfer(self) -> None:
        self.transfer_id = None
        self.product_id = 0
        self.total_bytes = 0
        self.total_packets = 0
        self.file_crc = 0
        self.packet_data_bytes = DATA_BYTES
        self.packets = {}
        self.end_seen = False
        self.next_retry_request_s = 0.0
        self.last_packet_s = 0.0

    def finalize_to_dir(self) -> None:
        assert self.output_dir is not None
        blob = self.reconstruct()
        actual_crc = crc16_ccitt(blob)
        crc_ok = actual_crc == self.file_crc
        self.received_count += 1
        self.output_dir.mkdir(parents=True, exist_ok=True)

        suffix = "" if crc_ok else ".badcrc"
        base = f"Dp_{time.strftime('%Y%m%d_%H%M%S')}"
        target = self.output_dir / f"{base}{self.ext}{suffix}"
        dupe = 1
        while target.exists():
            target = self.output_dir / f"{base}_{dupe:03d}{self.ext}{suffix}"
            dupe += 1
        target.write_bytes(blob)
        status = "ok" if crc_ok else f"CRC MISMATCH actual=0x{actual_crc:04x} expected=0x{self.file_crc:04x}"
        print(
            f"saved: {target.name} product={self.product_id} transfer={self.transfer_id} "
            f"bytes={len(blob)} packets={self.total_packets} [{status}]"
        )

    def run_directory(self, idle_timeout_s: float = 0.0) -> int:
        assert self.output_dir is not None
        self.output_dir.mkdir(parents=True, exist_ok=True)
        print(f"listening on {self.port}: saving completed transfers to {self.output_dir}/ (Ctrl-C to stop)")
        last_activity = time.monotonic()
        with serial.Serial(self.port, self.baud, timeout=0.2) as ser:
            try:
                while True:
                    packet = self.read_packet(ser)
                    if packet:
                        self.handle_packet(packet, ser)
                        last_activity = time.monotonic()
                    self.request_retries_if_due(ser)
                    if self.complete:
                        self.finalize_to_dir()
                        self.reset_transfer()
                        last_activity = time.monotonic()
                    elif (
                        idle_timeout_s > 0
                        and self.transfer_id is None
                        and (time.monotonic() - last_activity) > idle_timeout_s
                    ):
                        print("idle timeout reached; exiting")
                        break
            except KeyboardInterrupt:
                print("\nstopped by user")
        return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Payload serial port, usually ground Teensy SerialUSB2")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("payload_blob.bin"),
        help="Single-file output path; used only when --output-dir is not given",
    )
    parser.add_argument(
        "--output-dir",
        type=pathlib.Path,
        default=None,
        help="Directory to save every completed transfer in continuous-listen mode",
    )
    parser.add_argument(
        "--ext",
        default=".bin",
        help="Filename extension label for --output-dir files, e.g. .fdp",
    )
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument(
        "--idle-timeout",
        type=float,
        default=0.0,
        help="--output-dir mode: exit after this many idle seconds; 0 runs until Ctrl-C",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Print raw-stream heartbeat diagnostics",
    )
    args = parser.parse_args(argv)
    receiver = PayloadReceiver(
        args.port,
        args.baud,
        args.output,
        args.timeout,
        output_dir=args.output_dir,
        ext=args.ext,
        debug=args.debug,
    )
    if args.output_dir is not None:
        return receiver.run_directory(args.idle_timeout)
    return receiver.run()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
