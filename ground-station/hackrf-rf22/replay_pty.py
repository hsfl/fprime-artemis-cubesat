#!/usr/bin/env python3
"""Replay fixed-size channel-0 messages through a pseudo-serial port."""

from __future__ import annotations

import argparse
import signal
import time
from pathlib import Path

from bridge_core import VirtualChannel


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--message-bytes", type=int, default=128)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--startup-delay", type=float, default=8.0)
    parser.add_argument("--repeat", action="store_true")
    parser.add_argument("--symlink", type=Path, default=Path("/tmp/c3m-hackrf-gds"))
    args = parser.parse_args()

    if args.message_bytes < 1:
        parser.error("--message-bytes must be at least one")
    if args.interval < 0 or args.startup_delay < 0:
        parser.error("--interval and --startup-delay must not be negative")
    data = args.input.read_bytes()
    if not data or len(data) % args.message_bytes:
        parser.error("input must contain a whole number of fixed-size messages")
    messages = [data[i : i + args.message_bytes] for i in range(0, len(data), args.message_bytes)]

    stopping = False

    def stop(_signum: int, _frame: object) -> None:
        nonlocal stopping
        stopping = True

    channel = VirtualChannel(
        0,
        args.symlink,
        max_backlog_bytes=max(1 << 20, args.message_bytes),
    )
    try:
        channel.open()
        signal.signal(signal.SIGINT, stop)
        signal.signal(signal.SIGTERM, stop)
        print(
            f"PTY_READY path={args.symlink} target={channel.slave_name} "
            f"messages={len(messages)}",
            flush=True,
        )
        time.sleep(args.startup_delay)

        while not stopping:
            for index, message in enumerate(messages):
                if stopping:
                    break
                channel.queue_downlink(message)
                while channel.backlog_bytes and not stopping:
                    channel.flush_downlink()
                    if channel.backlog_bytes:
                        time.sleep(0.001)
                if stopping:
                    break
                print(f"REPLAY index={index} bytes={len(message)}", flush=True)
                time.sleep(args.interval)
            if not args.repeat:
                break
    finally:
        channel.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
