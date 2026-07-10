#!/usr/bin/env python3
"""Decode EPSCoR C3M Lepton F Prime data products."""

from __future__ import annotations

import argparse
import datetime as dt
import glob
import json
import os
import shutil
import statistics
import struct
import subprocess
import sys
import zlib
from pathlib import Path
from typing import Any


WIDTH = 160
HEIGHT = 120
NUM_PIXELS = WIDTH * HEIGHT
PIXEL_DATA_OFFSET = 76
PIXEL_DATA_BYTES = NUM_PIXELS * 2


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "ArtemisRpiTeensy_N2").is_dir() and (candidate / "ground-station").is_dir():
            return candidate
    return start


def find_dictionary(script_path: Path) -> Path | None:
    repo_root = find_repo_root(script_path.parent)
    patterns = [
        repo_root
        / "ArtemisRpiTeensy_N2"
        / "build-artifacts"
        / "*"
        / "ArtemisRpiTeensyDeployment"
        / "dict"
        / "ArtemisRpiTeensyDeploymentTopologyDictionary.json",
        repo_root / "ArtemisRpiTeensy_N2" / "build-artifacts" / "*" / "*" / "dict" / "*Dictionary.json",
    ]
    for pattern in patterns:
        hits = glob.glob(str(pattern))
        if hits:
            hits.sort(key=lambda value: Path(value).stat().st_mtime, reverse=True)
            return Path(hits[0])
    return None


def run_fprime_dp_decode(bin_file: Path, dictionary: Path, out_json: Path) -> None:
    cmd = [
        "fprime-dp",
        "decode",
        "--bin-file",
        str(bin_file),
        "--dictionary",
        str(dictionary),
        "--output",
        str(out_json),
    ]
    try:
        completed = subprocess.run(cmd, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except FileNotFoundError:
        raise SystemExit("fprime-dp not found on PATH; activate ArtemisRpiTeensy_N2/fprime-venv first")
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"fprime-dp decode failed with exit code {exc.returncode}")
    if completed.stdout:
        print(completed.stdout, file=sys.stderr, end="")
    if completed.stderr:
        print(completed.stderr, file=sys.stderr, end="")
    if not out_json.exists():
        raise SystemExit(f"decode reported success but did not write {out_json}")


def unwrap_number(value: Any) -> int | float | None:
    if isinstance(value, dict):
        value = value.get("value")
    if isinstance(value, (int, float)):
        return value
    return None


def extract_pixels(decoded: Any) -> list[int]:
    def walk(node: Any) -> list[int] | None:
        if isinstance(node, list):
            if len(node) == NUM_PIXELS:
                numbers = [unwrap_number(item) for item in node]
                if all(isinstance(item, (int, float)) for item in numbers):
                    return [int(item) for item in numbers]
            for item in node:
                found = walk(item)
                if found is not None:
                    return found
        elif isinstance(node, dict):
            for value in node.values():
                found = walk(value)
                if found is not None:
                    return found
        return None

    pixels = walk(decoded)
    if pixels is None:
        raise SystemExit(f"no {NUM_PIXELS}-element Lepton pixel array found in decoded data product")
    return pixels


def find_captured_at(decoded: Any) -> str | None:
    def unwrap_time(node: Any) -> tuple[int, int] | None:
        if not isinstance(node, dict):
            return None
        seconds = node.get("seconds")
        micros = node.get("microseconds", node.get("useconds", 0))
        if isinstance(seconds, dict):
            seconds = seconds.get("value")
        if isinstance(micros, dict):
            micros = micros.get("value")
        if isinstance(seconds, int) and isinstance(micros, int):
            return seconds, micros
        return None

    def walk(node: Any) -> tuple[int, int] | None:
        found = unwrap_time(node)
        if found is not None:
            return found
        if isinstance(node, dict):
            for value in node.values():
                found = walk(value)
                if found is not None:
                    return found
        elif isinstance(node, list):
            for item in node:
                found = walk(item)
                if found is not None:
                    return found
        return None

    found = walk(decoded)
    if found is None:
        return None
    seconds, micros = found
    timestamp = dt.datetime.fromtimestamp(seconds + micros / 1_000_000, dt.timezone.utc)
    return timestamp.isoformat()


def centikelvin_to_celsius(pixels: list[int]) -> list[float]:
    return [(pixel / 100.0) - 273.15 for pixel in pixels]


def write_csv(path: Path, values_c: list[float | None], captured_at: str | None) -> None:
    with path.open("w", encoding="utf-8") as handle:
        if captured_at:
            handle.write(f"# CAPTURED_AT,{captured_at}\n")
        for row in range(HEIGHT):
            offset = row * WIDTH
            line = ",".join("NaN" if value is None else f"{value:.2f}" for value in values_c[offset : offset + WIDTH])
            handle.write(line)
            handle.write("\n")


def hot_color(value: float) -> tuple[int, int, int]:
    value = max(0.0, min(1.0, value))
    red = min(1.0, value * 3.0)
    green = min(1.0, max(0.0, value * 3.0 - 1.0))
    blue = min(1.0, max(0.0, value * 3.0 - 2.0))
    return round(red * 255), round(green * 255), round(blue * 255)


def write_png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_fallback_png(path: Path, values_c: list[float | None], scale: int = 4) -> None:
    valid_values = [value for value in values_c if value is not None]
    if not valid_values:
        raise ValueError("thermal product contains no valid pixels")
    min_c = min(valid_values)
    max_c = max(valid_values)
    span = max(max_c - min_c, 1.0)
    rows: list[bytes] = []
    for row in range(HEIGHT):
        out = bytearray()
        offset = row * WIDTH
        for value in values_c[offset : offset + WIDTH]:
            color = b"\xff\xff\xff" if value is None else bytes(hot_color((value - min_c) / span))
            out.extend(color * scale)
        row_bytes = bytes(out)
        for _ in range(scale):
            rows.append(row_bytes)

    png_width = WIDTH * scale
    png_height = HEIGHT * scale
    scanlines = b"".join(b"\x00" + row for row in rows)
    ihdr = struct.pack(">IIBBBBB", png_width, png_height, 8, 2, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + write_png_chunk(b"IHDR", ihdr)
        + write_png_chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + write_png_chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def open_image(path: Path) -> None:
    if sys.platform == "darwin":
        subprocess.Popen(["open", str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return
    if os.name == "nt":
        os.startfile(path)  # type: ignore[attr-defined]
        return
    opener = shutil.which("xdg-open") or shutil.which("wslview")
    if opener:
        subprocess.Popen([opener, str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def write_png(path: Path, values_c: list[float | None], title: str, no_show: bool) -> bool:
    try:
        import matplotlib.pyplot as plt  # type: ignore
    except ImportError:
        write_fallback_png(path, values_c)
        if not no_show:
            open_image(path)
        return True

    grid = [
        [float("nan") if value is None else value for value in values_c[row * WIDTH : (row + 1) * WIDTH]]
        for row in range(HEIGHT)
    ]
    fig, ax = plt.subplots(figsize=(8, 6))
    cmap = plt.get_cmap("hot").copy()
    cmap.set_bad(color="white")
    image = ax.imshow(grid, cmap=cmap, aspect="equal", interpolation="nearest")
    fig.colorbar(image, ax=ax, label="Temperature (C)")
    ax.set_title(title)
    ax.set_xlabel("Column")
    ax.set_ylabel("Row")
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    if not no_show:
        plt.show()
    plt.close(fig)
    return True


def decode_partial_product(
    bin_file: Path,
    outdir: Path,
    missing_packet_indices: list[int] | tuple[int, ...],
    packet_data_bytes: int,
    *,
    no_show: bool = True,
) -> dict[str, Any]:
    """Decode a positional Lepton FDP with known channel-1 packet gaps.

    This deliberately bypasses ``fprime-dp`` because the container checksum is
    invalid. Only the fixed Lepton pixel record is recovered, and any sample
    whose two source bytes intersect a missing packet is represented as null.
    """

    blob = bin_file.read_bytes()
    if len(blob) < PIXEL_DATA_OFFSET + PIXEL_DATA_BYTES:
        raise ValueError(f"partial Lepton product has unsafe size {len(blob)}")
    missing = {int(index) for index in missing_packet_indices}
    values_c: list[float | None] = []
    raw_values: list[int | None] = []
    for pixel_index in range(NUM_PIXELS):
        byte_offset = PIXEL_DATA_OFFSET + pixel_index * 2
        source_packets = {byte_offset // packet_data_bytes, (byte_offset + 1) // packet_data_bytes}
        if source_packets & missing:
            raw_values.append(None)
            values_c.append(None)
            continue
        raw = struct.unpack_from(">H", blob, byte_offset)[0]
        raw_values.append(raw)
        values_c.append((raw / 100.0) - 273.15)

    valid_values = [value for value in values_c if value is not None]
    if not valid_values:
        raise ValueError("partial Lepton product has no recoverable thermal samples")

    outdir.mkdir(parents=True, exist_ok=True)
    out_json = outdir / "payload.json"
    out_csv = outdir / "payload.csv"
    out_png = outdir / "payload.png"
    out_json.write_text(
        json.dumps(
            {
                "partial": True,
                "width": WIDTH,
                "height": HEIGHT,
                "pixels_centikelvin": raw_values,
                "missing_packet_indices": sorted(missing),
            },
            indent=2,
            allow_nan=False,
        )
        + "\n",
        encoding="utf-8",
    )
    write_csv(out_csv, values_c, None)
    title = (
        f"Partial thermal product — {len(valid_values)}/{NUM_PIXELS} pixels\n"
        f"min {min(valid_values):.1f}C  max {max(valid_values):.1f}C  "
        f"mean {statistics.fmean(valid_values):.1f}C"
    )
    write_png(out_png, values_c, title, no_show)
    return {
        "input": str(bin_file),
        "dictionary": None,
        "json": str(out_json),
        "csv": str(out_csv),
        "png": str(out_png),
        "width": WIDTH,
        "height": HEIGHT,
        "pixels": NUM_PIXELS,
        "valid_pixels": len(valid_values),
        "missing_pixels": NUM_PIXELS - len(valid_values),
        "received_percent": round(100.0 * len(valid_values) / NUM_PIXELS, 3),
        "captured_at": None,
        "min_c": round(min(valid_values), 2),
        "max_c": round(max(valid_values), 2),
        "mean_c": round(statistics.fmean(valid_values), 2),
        "partial": True,
    }


def decode_product(args: argparse.Namespace) -> dict[str, Any]:
    bin_file = args.bin_file.resolve()
    if not bin_file.exists():
        raise SystemExit(f"file not found: {bin_file}")

    dictionary = args.dictionary or find_dictionary(Path(__file__).resolve())
    if dictionary is None or not dictionary.exists():
        raise SystemExit("dictionary not found; pass --dictionary <path>")

    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    stem = bin_file.stem
    out_json = outdir / f"{stem}.json"
    out_csv = outdir / f"{stem}.csv"
    out_png = outdir / f"{stem}.png"

    run_fprime_dp_decode(bin_file, dictionary, out_json)
    decoded = json.loads(out_json.read_text(encoding="utf-8"))
    raw_pixels = extract_pixels(decoded)
    values_c = centikelvin_to_celsius(raw_pixels)
    captured_at = find_captured_at(decoded)

    write_csv(out_csv, values_c, captured_at)
    png_written = False
    if not args.no_png:
        title = (
            f"{stem}\n"
            f"min {min(values_c):.1f}C  max {max(values_c):.1f}C  mean {statistics.fmean(values_c):.1f}C"
        )
        png_written = write_png(out_png, values_c, title, args.no_show)

    return {
        "input": str(bin_file),
        "dictionary": str(dictionary.resolve()),
        "json": str(out_json),
        "csv": str(out_csv),
        "png": str(out_png) if png_written else None,
        "width": WIDTH,
        "height": HEIGHT,
        "pixels": len(raw_pixels),
        "captured_at": captured_at,
        "min_c": round(min(values_c), 2),
        "max_c": round(max(values_c), 2),
        "mean_c": round(statistics.fmean(values_c), 2),
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bin_file", type=Path, help="Path to a Lepton .fdp data product")
    parser.add_argument("--dictionary", type=Path, help="Deployment topology dictionary JSON")
    parser.add_argument("--outdir", type=Path, default=Path("./data"), help="Output directory")
    parser.add_argument("--summary", action="store_true", help="Print compact JSON summary to stdout")
    parser.add_argument("--no-png", action="store_true", help="Skip PNG generation")
    parser.add_argument("--no-show", action="store_true", help="Save PNG without opening a window")
    args = parser.parse_args(argv)

    summary = decode_product(args)
    if args.summary:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print(f"Wrote JSON: {summary['json']}")
        print(f"Wrote CSV : {summary['csv']}")
        if summary["png"]:
            print(f"Wrote PNG : {summary['png']}")
        else:
            print("PNG skipped: matplotlib unavailable or --no-png was set")
        print(
            f"Frame: {summary['width']}x{summary['height']} "
            f"{summary['min_c']:.1f}..{summary['max_c']:.1f} C "
            f"mean {summary['mean_c']:.1f} C"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
