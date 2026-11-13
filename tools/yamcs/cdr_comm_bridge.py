#!/usr/bin/env python3
"""Run the packaged F' TCP/UDP adapters with paced stock TM-frame egress."""

from __future__ import annotations

import argparse
import json
import logging
import math
import os
from pathlib import Path
import signal
import threading
import time

from fprime_gds.common.communication.adapters.tcp_fast import TcpFastClientAdapter
from fprime_gds.common.communication.adapters.udp_fast import UdpFastAdapter
from fprime_gds.common.communication.bridge.bridge import PacketBridge
from fprime_gds.common.communication.ccsds.tm_frame_aggregator import (
    TmFrameAggregatorFramerDeframer,
)

UDP_SEND_PORT = 50000
UDP_RECV_PORT = 50001
MIN_FRAME_INTERVAL_MS = 20.0


def positive_interval(value: str) -> float:
    try:
        interval = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("frame interval must be a number of milliseconds") from exc
    if not math.isfinite(interval) or interval <= 0:
        raise argparse.ArgumentTypeError("frame interval must be finite and greater than zero")
    return interval


def dictionary_constants(dictionary: Path) -> tuple[int, int]:
    try:
        data = json.loads(dictionary.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read F' dictionary {dictionary}: {exc}") from exc
    constants = {item.get("qualifiedName"): item.get("value")
                 for item in data.get("constants", [])}
    try:
        frame_size = int(constants["ComCfg.TmFrameFixedSize"])
        scid = int(constants["ComCfg.SpacecraftId"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError("dictionary must define integer ComCfg.TmFrameFixedSize and ComCfg.SpacecraftId") from exc
    if frame_size < 8 or not 0 <= scid <= 0x3FF:
        raise ValueError(f"invalid CCSDS frame constants: size={frame_size}, SCID={scid}")
    return frame_size, scid


class PacedUdpFastAdapter(UdpFastAdapter):
    """Stock UDP adapter with minimum spacing between outgoing TM datagrams."""

    def __init__(self, *args, frame_interval_ms: float = MIN_FRAME_INTERVAL_MS,
                 clock=time.monotonic, sleeper=time.sleep, **kwargs):
        if not math.isfinite(frame_interval_ms) or frame_interval_ms <= 0:
            raise ValueError("frame interval must be finite and greater than zero")
        super().__init__(*args, **kwargs)
        self.frame_interval = frame_interval_ms / 1000.0
        self._clock = clock
        self._sleep = sleeper
        self._next_write_at = 0.0
        self._pace_lock = threading.Lock()
        self._writes = 0
        self._written_bytes = 0
        self._window_started_at = clock()

    def write(self, frame) -> bool:
        with self._pace_lock:
            delay = self._next_write_at - self._clock()
            if delay > 0:
                self._sleep(delay)
            sent = super().write(frame)
            # Schedule from actual completion: a delayed write never creates a catch-up burst.
            self._next_write_at = self._clock() + self.frame_interval
            self._writes += 1
            if sent:
                self._written_bytes += len(frame)
            if self._writes % 100 == 0:
                elapsed = max(self._clock() - self._window_started_at, 1e-9)
                logging.getLogger(__name__).info(
                    "paced UDP TM egress frames=%d sent_bytes=%d rate=%.1f frames/s %.1f kB/s",
                    self._writes, self._written_bytes, self._writes / elapsed,
                    self._written_bytes / elapsed / 1000.0,
                )
        return sent


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Bridge the Zero W stock F' TCP server to Yamcs loopback UDP using stock F' adapters; pace TM frames."
    )
    parser.add_argument("--dictionary", required=True, type=Path,
                        help="matching F' CDR TCP topology JSON dictionary")
    parser.add_argument("--address", required=True, help="connectable Zero W TCP server address")
    parser.add_argument("--port", required=True, type=int, help="Zero W Drv.TcpServer port")
    parser.add_argument("--frame-interval-ms", type=positive_interval, default=MIN_FRAME_INTERVAL_MS,
                        help="minimum gap between outgoing TM UDP frames (default/minimum: 20 ms)")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    dictionary = args.dictionary.expanduser().resolve(strict=True)
    if not dictionary.is_file():
        parser.error(f"dictionary is not a file: {dictionary}")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    try:
        frame_size, scid = dictionary_constants(dictionary)
    except ValueError as exc:
        parser.error(str(exc))

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    if args.frame_interval_ms < MIN_FRAME_INTERVAL_MS:
        logging.warning("Requested TM pacing %.3f ms is below the ground minimum; using %.3f ms (50 frames/s)",
                        args.frame_interval_ms, MIN_FRAME_INTERVAL_MS)
        args.frame_interval_ms = MIN_FRAME_INTERVAL_MS
    stop_requested = threading.Event()
    bridge = PacketBridge(
        TcpFastClientAdapter(args.address, args.port),
        TmFrameAggregatorFramerDeframer(frame_size=frame_size, scid=scid),
        PacedUdpFastAdapter(
            "127.0.0.1", UDP_SEND_PORT, UDP_RECV_PORT, "127.0.0.1", ["127.0.0.1"],
            frame_interval_ms=args.frame_interval_ms,
        ),
        failure_handler=stop_requested.set,
    )

    def request_stop(_signum, _frame):
        stop_requested.set()

    previous_handlers = {sig: signal.signal(sig, request_stop)
                         for sig in (signal.SIGINT, signal.SIGTERM)}
    started = False
    try:
        bridge.start()
        started = True
        logging.info(
            "paced F' bridge ready pid=%d tcp=%s:%d frame=%dB scid=%d interval=%.3fms UDP=127.0.0.1:%d/%d",
            os.getpid(), args.address, args.port, frame_size, scid,
            args.frame_interval_ms, UDP_SEND_PORT, UDP_RECV_PORT,
        )
        stop_requested.wait()
    except Exception:
        logging.exception("paced F' bridge failed")
        return 1
    finally:
        if started:
            bridge.stop()
        for sig, handler in previous_handlers.items():
            signal.signal(sig, handler)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
