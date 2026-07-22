#!/usr/bin/env python3
"""Run and capture the dedicated ground-Teensy TX load-test image."""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

import serial


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--debug-port", required=True, help="Ground Teensy second/debug USB port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=600.0, help="Overall test timeout in seconds")
    parser.add_argument("--log", type=pathlib.Path, help="Optional debug log output path")
    subparsers = parser.add_subparsers(dest="mode", required=True)

    rf = subparsers.add_parser("rf", help="Ground-only maximum-packet RF TX soak")
    rf.add_argument("--packets", type=int, default=100)
    rf.add_argument("--interval-ms", type=int, default=100)
    rf.add_argument("--power-dbm", type=int, choices=(28, 29, 30), default=30)

    usb = subparsers.add_parser("usb", help="Teensy-to-GDS USB CDC TX load")
    usb.add_argument("--data-port", required=True, help="Ground Teensy first/GDS USB port")
    usb.add_argument("--bytes", type=int, default=1_000_000)
    usb.add_argument("--chunk-bytes", type=int, default=220)
    return parser


def command_for(args: argparse.Namespace) -> str:
    if args.mode == "rf":
        return f"load rf {args.packets} {args.interval_ms} {args.power_dbm}"
    return f"load usb {args.bytes} {args.chunk_bytes}"


def run(args: argparse.Namespace) -> int:
    debug = serial.Serial(args.debug_port, args.baud, timeout=0.05, write_timeout=1)
    data = None
    log_handle = None
    data_bytes = 0
    complete = False
    line_buffer = bytearray()

    try:
        if args.mode == "usb":
            data = serial.Serial(args.data_port, args.baud, timeout=0)
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            log_handle = args.log.open("w", encoding="utf-8")

        debug.reset_input_buffer()
        if data is not None:
            data.reset_input_buffer()
        command = command_for(args)
        print(f"sending: {command}")
        debug.write((command + "\n").encode("ascii"))
        debug.flush()

        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            chunk = debug.read(debug.in_waiting or 1)
            if chunk:
                line_buffer.extend(chunk)
                while b"\n" in line_buffer:
                    raw_line, _, remainder = line_buffer.partition(b"\n")
                    line_buffer = bytearray(remainder)
                    line = raw_line.decode("utf-8", errors="replace").rstrip("\r")
                    print(line)
                    if log_handle is not None:
                        log_handle.write(line + "\n")
                        log_handle.flush()
                    if "[GDS_LOAD] LOAD_COMPLETE" in line:
                        complete = True

            if data is not None:
                available = data.in_waiting
                if available:
                    data_bytes += len(data.read(available))

            if complete and (data is None or data_bytes >= args.bytes):
                break

        if not complete:
            debug.write(b"load stop\n")
            debug.flush()
            print("test timed out before LOAD_COMPLETE", file=sys.stderr)
            return 2
        if data is not None and data_bytes < args.bytes:
            print(
                f"firmware completed but host drained only {data_bytes}/{args.bytes} bytes",
                file=sys.stderr,
            )
            return 3

        if data is not None:
            print(f"host_data_bytes={data_bytes}")
        return 0
    except KeyboardInterrupt:
        debug.write(b"load stop\n")
        debug.flush()
        print("test stopped by operator", file=sys.stderr)
        return 130
    finally:
        if log_handle is not None:
            log_handle.close()
        if data is not None:
            data.close()
        debug.close()


def main() -> int:
    args = build_parser().parse_args()
    if args.mode == "rf":
        if not 1 <= args.packets <= 100_000 or not 10 <= args.interval_ms <= 60_000:
            raise SystemExit("RF packets must be 1..100000 and interval must be 10..60000 ms")
    else:
        if not 1 <= args.bytes <= 100_000_000 or not 1 <= args.chunk_bytes <= 220:
            raise SystemExit("USB bytes must be 1..100000000 and chunk size must be 1..220")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
