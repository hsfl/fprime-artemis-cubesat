#!/usr/bin/env python3
"""Route stock TCP file packets to disk and other packets to Yamcs."""

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
from fprime_gds.common.communication.ccsds.chain import RawSpaceDataLinkFramerDeframer
from spacepackets.ccsds.spacepacket import SpacePacketHeader

from local_file_receiver import (
    BucketPublisher, DEFAULT_MAX_FILE_BYTES, DEFAULT_MAX_SPOOL_BYTES, FILE_APID,
    LocalFileReceiver, PublicationWorker,
)
from yamcs_client import loopback_url

UDP_RECV_PORT = 50001
UDP_PACKET_PORT = 50000


def positive_interval(value: str) -> float:
    try:
        interval = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("timeout must be a number of seconds") from exc
    if not math.isfinite(interval) or interval <= 0:
        raise argparse.ArgumentTypeError("timeout must be finite and greater than zero")
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


class ObservedTcpClient(TcpFastClientAdapter):
    """Observe actual stock socket replacements under the adapter's lock."""

    def __init__(self, *args, **kwargs):
        self.connection_epoch = 0
        self._connection = None
        super().__init__(*args, **kwargs)

    @property
    def connection(self):
        return self._connection

    @connection.setter
    def connection(self, value):
        # The stock adapter performs replacement under self.lock (apart from
        # construction). Observing the assignment avoids a stale outer snapshot
        # of an old socket incrementing the generation after reconnection.
        if value is not self._connection:
            self._connection = value
            self.connection_epoch += 1

    def connection_state(self):
        with self.lock:
            return self.connection, self.connection_epoch


class PacketRouterBridge(PacketBridge):
    """Keep stock adapter/uplink behavior, consuming APID 3 before UDP."""

    def __init__(self, flight, ground, receiver, frame_size, scid, *, failure_handler=None):
        self.receiver = receiver
        self.frame_size = frame_size
        self.scid = scid
        self.counters = {name: 0 for name in (
            "tcp_bytes", "file_packets", "nonfile_packets", "udp_write_failures", "uplink_packets",
            "discarded_bytes", "connection_changes", "pending_tcp_bytes", "sequence_gaps",
            "oversize_nonfile_packets")}
        self.apid_counts = {}
        self.apid_sequences = {}
        self.last_tcp_unix = 0.0
        self.last_packet_unix = 0.0
        super().__init__(flight, self._decoder(), ground, failure_handler=failure_handler)

    def _decoder(self):
        decoder = RawSpaceDataLinkFramerDeframer(scid=self.scid, vcid=1, frame_size=self.frame_size)
        # Yamcs supplies complete TC frames on UDP50001; keep those unchanged.
        decoder.frame = lambda data: data
        return decoder

    def route(self, packet):
        header = SpacePacketHeader.unpack(packet)
        self.last_packet_unix = time.time()
        key = str(header.apid) if header.apid < 16 else "other"
        self.apid_counts[key] = self.apid_counts.get(key, 0) + 1
        if header.apid < 16:
            expected = self.apid_sequences.get(header.apid)
            if expected is not None and header.seq_count != expected:
                self.counters["sequence_gaps"] += 1
                if self.counters["sequence_gaps"] <= 10:
                    logging.warning("TCP packet sequence gap APID=%d expected=%d got=%d",
                                    header.apid, expected, header.seq_count)
            self.apid_sequences[header.apid] = (header.seq_count + 1) % 16384
        if header.apid == FILE_APID:
            self.counters["file_packets"] += 1
            self.receiver.receive(bytes(packet))
        else:
            self.counters["nonfile_packets"] += 1
            if len(packet) > 65507:
                self.counters["oversize_nonfile_packets"] += 1
                if self.counters["oversize_nonfile_packets"] <= 5:
                    logging.error("Nonfile packet APID=%d size=%d exceeds UDP payload limit", header.apid, len(packet))
                return
            self.write_or_warn(self.ground, "ground", packet)

    def write_or_warn(self, adapter, side, data):
        sent = adapter.write(data)
        if side == "ground" and not sent:
            self.counters["udp_write_failures"] += 1
        elif side == "flight" and sent:
            self.counters["uplink_packets"] += 1
        if sent:
            self.write_failed.discard(side)
        elif side not in self.write_failed:
            self.write_failed.add(side)
            logging.warning("Failed to write %d bytes to %s adapter", len(data), side)

    def statistics(self):
        return {**self.counters, "apid_packets": dict(self.apid_counts),
                "last_tcp_unix": self.last_tcp_unix, "last_packet_unix": self.last_packet_unix}

    def downlink_loop(self):
        pending = b""
        epoch = self.flight.connection_epoch
        try:
            while self.running:
                data = self.flight.read()
                connection, new_epoch = self.flight.connection_state()
                if new_epoch != epoch:
                    self.counters["connection_changes"] += 1
                    self.receiver.abort("TCP connection changed")
                    self.framer = self._decoder()
                    self.apid_sequences.clear()
                    pending = b""
                    epoch = new_epoch
                    if connection is None:
                        continue
                if not data:
                    continue
                self.counters["tcp_bytes"] += len(data)
                self.last_tcp_unix = time.time()
                pending += data
                if len(pending) > 2 * 65542 + self.frame_size:
                    self.receiver.abort("oversize undecoded TCP stream")
                    self.framer = self._decoder()
                    pending = b""
                    continue
                packets, pending, discarded = self.framer.deframe_all(pending, no_copy=True)
                self.counters["pending_tcp_bytes"] = len(pending)
                if discarded:
                    self.counters["discarded_bytes"] += len(discarded)
                    self.receiver.abort("invalid or missing TM frame bytes")
                    logging.warning("Discarded %d undecodable TCP bytes", len(discarded))
                for packet in packets:
                    self.route(packet)
        except Exception as error:
            self.report_failure("Downlink", error)
        finally:
            self.receiver.abort("TCP receiver stopped")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Decode stock TCP TM frames; receive files locally and send other packets to Yamcs."
    )
    parser.add_argument("--dictionary", required=True, type=Path,
                        help="matching F' CDR TCP topology JSON dictionary")
    parser.add_argument("--address", required=True, help="connectable Zero W TCP server address")
    parser.add_argument("--port", required=True, type=int, help="Zero W Drv.TcpServer port")
    parser.add_argument("--station", type=Path, default=Path.home() / "yamcs-station")
    parser.add_argument("--receive-dir", type=Path, help="verified-file spool (default: station/received-files)")
    parser.add_argument("--yamcs-url", type=loopback_url, default="http://127.0.0.1:8090")
    parser.add_argument("--instance", default="fprime-project", help="Yamcs instance (bucket API is global)")
    parser.add_argument("--no-publish", action="store_true", help="save verified files locally without HTTP publication")
    parser.add_argument("--file-idle-timeout", type=positive_interval, default=30.0, help="seconds without file data")
    parser.add_argument("--max-file-bytes", type=int, default=DEFAULT_MAX_FILE_BYTES)
    parser.add_argument("--max-spool-bytes", type=int, default=DEFAULT_MAX_SPOOL_BYTES)
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
    station = args.station.expanduser().resolve()
    receive_dir = args.receive_dir.expanduser().resolve() if args.receive_dir else station / "received-files"
    credentials = station / "Yamcs-access.txt"
    publisher = None if args.no_publish else PublicationWorker(BucketPublisher(
        args.yamcs_url, credentials=credentials if credentials.is_file() else None))
    try:
        receiver = LocalFileReceiver(receive_dir, on_saved=publisher.submit if publisher else None,
                                     idle_timeout=args.file_idle_timeout,
                                     max_file_bytes=args.max_file_bytes, max_spool_bytes=args.max_spool_bytes)
    except (OSError, ValueError, RuntimeError) as error:
        if publisher:
            publisher.close()
        parser.error(str(error))
    stop_requested = threading.Event()
    bridge = PacketRouterBridge(
        ObservedTcpClient(args.address, args.port),
        UdpFastAdapter("127.0.0.1", UDP_PACKET_PORT, UDP_RECV_PORT, "127.0.0.1", ["127.0.0.1"]),
        receiver, frame_size, scid,
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
            "local-file F' bridge ready pid=%d tcp=%s:%d frame=%dB scid=%d packet-UDP=127.0.0.1:%d/%d spool=%s",
            os.getpid(), args.address, args.port, frame_size, scid,
            UDP_PACKET_PORT, UDP_RECV_PORT, receive_dir,
        )
        next_progress_log = time.monotonic() + 5
        while not stop_requested.wait(0.25):
            receiver.bridge_metrics = bridge.statistics()
            receiver.tick()
            if time.monotonic() >= next_progress_log:
                logging.info("bridge progress %s", json.dumps(bridge.statistics(), sort_keys=True))
                next_progress_log = time.monotonic() + 5
    except Exception:
        logging.exception("F' packet bridge failed")
        return 1
    finally:
        if started:
            bridge.stop()
        receiver.close()
        if publisher:
            publisher.close()
        for sig, handler in previous_handlers.items():
            signal.signal(sig, handler)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
