#!/usr/bin/env python3
"""Stateful neutron-count payload simulator for the FlatSat demo."""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import time
from pathlib import Path
from typing import Any


DEFAULT_WINDOW_SECONDS = 1.0


def load_cursor(path: Path) -> int:
    if not path.exists():
        return 0
    try:
        data = json.loads(path.read_text())
        return max(0, int(data.get("cursor", 0)))
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        return 0


def save_cursor(path: Path, cursor: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"cursor": cursor}, indent=2) + "\n")


def load_dataset(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"dataset has no rows: {path}")
    required = {"t_s", "counts", "flag"}
    missing = required.difference(rows[0].keys())
    if missing:
        raise ValueError(f"dataset missing columns: {', '.join(sorted(missing))}")
    return rows


def slice_rows(
    rows: list[dict[str, str]],
    start: int,
    count: int,
    end_policy: str,
) -> tuple[list[dict[str, str]], int, bool]:
    if count <= 0:
        raise ValueError("capture row count must be positive")

    dataset_len = len(rows)
    start = start % dataset_len

    if start + count <= dataset_len:
        return rows[start:start + count], (start + count) % dataset_len, False

    if end_policy == "error":
        raise ValueError("capture request runs past dataset end")

    if end_policy == "short":
        return rows[start:], dataset_len, True

    if end_policy == "clamp":
        tail = rows[start:]
        if len(tail) < count:
            tail.extend([rows[-1]] * (count - len(tail)))
        return tail, dataset_len - 1, True

    captured: list[dict[str, str]] = []
    cursor = start
    wrapped = False
    for _ in range(count):
        captured.append(rows[cursor])
        cursor += 1
        if cursor >= dataset_len:
            cursor = 0
            wrapped = True
    return captured, cursor, wrapped


def write_capture(path: Path, rows: list[dict[str, str]]) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=["t_s", "counts", "flag"])
        writer.writeheader()
        writer.writerows(rows)
    return path.stat().st_size


def next_available_path(path: Path) -> Path:
    if not path.exists():
        return path
    for index in range(1, 10000):
        candidate = path.with_name(f"{path.stem}_{index:03d}{path.suffix}")
        if not candidate.exists():
            return candidate
    raise RuntimeError(f"could not allocate unique capture filename near {path}")


def emit_summary(summary: dict[str, Any], output_format: str) -> None:
    if output_format == "json":
        print(json.dumps(summary, sort_keys=True))
        return
    for key in sorted(summary):
        print(f"{key}={summary[key]}")


def capture(args: argparse.Namespace) -> int:
    dataset = Path(args.dataset)
    cursor_path = Path(args.cursor)
    output_dir = Path(args.output_dir)
    rows = load_dataset(dataset)

    row_count = max(1, int(math.ceil(args.duration_seconds / args.window_seconds)))
    start_cursor = load_cursor(cursor_path)
    captured, next_cursor, wrapped = slice_rows(rows, start_cursor, row_count, args.end_policy)
    save_cursor(cursor_path, next_cursor)

    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    output_path = next_available_path(output_dir / f"neutron_capture_{stamp}_{start_cursor:05d}_{row_count:05d}.csv")
    output_bytes = write_capture(output_path, captured)

    counts = [int(row["counts"]) for row in captured]
    saa_rows = sum(1 for row in captured if row["flag"] == "SAA")
    summary = {
        "dataset": str(dataset),
        "duration_seconds": args.duration_seconds,
        "end_policy": args.end_policy,
        "max_counts": max(counts),
        "next_cursor": next_cursor,
        "output_bytes": output_bytes,
        "output_path": str(output_path),
        "rows": len(captured),
        "saa_rows": saa_rows,
        "start_cursor": start_cursor,
        "total_counts": sum(counts),
        "window_seconds": args.window_seconds,
        "wrapped": int(wrapped),
        "cleanup_hint": f"Cleanup old downlink CSVs in {output_dir}",
    }
    emit_summary(summary, args.format)
    return 0


def build_parser() -> argparse.ArgumentParser:
    default_root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture_parser = subparsers.add_parser("capture", help="Capture a duration of neutron samples")
    capture_parser.add_argument("--dataset", default=default_root / "neutron_data.csv", type=Path)
    capture_parser.add_argument("--duration-seconds", required=True, type=float)
    capture_parser.add_argument("--window-seconds", default=DEFAULT_WINDOW_SECONDS, type=float)
    capture_parser.add_argument("--cursor", default=default_root / ".neutron_payload_cursor.json", type=Path)
    capture_parser.add_argument("--output-dir", default=default_root / "captures", type=Path)
    capture_parser.add_argument(
        "--end-policy",
        choices=("wrap", "clamp", "error", "short"),
        default="wrap",
        help="Dataset-end behavior when capture duration exceeds remaining rows",
    )
    capture_parser.add_argument("--format", choices=("kv", "json"), default="kv")
    capture_parser.set_defaults(func=capture)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args))
    except Exception as exc:
        print(f"error={exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
