#!/usr/bin/env python3
"""Decode and view the standard-FDP Boson 320 raw-count data product."""

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


WIDTH = 320
HEIGHT = 256
NUM_PIXELS = WIDTH * HEIGHT
PIXEL_BYTES = NUM_PIXELS * 2
FW_PACKET_DP = 5
BOSON_CONTAINER_ID = 0x1002A000
FDP_SIGNATURE = struct.pack(">HI", FW_PACKET_DP, BOSON_CONTAINER_ID)
# FwSizeStoreType=U32 and Fw.TimeValue uses an 11-byte timeTag. The fixed
# record's pixels therefore begin at byte 78 in the serialized FDP.
PIXEL_DATA_OFFSET = 78


def read_fdp_container_id(path: Path) -> int | None:
    """Return the verified standard-FDP container ID, if present."""

    try:
        with path.open("rb") as stream:
            header = stream.read(struct.calcsize(">HI"))
    except OSError:
        return None
    if len(header) != struct.calcsize(">HI"):
        return None
    packet_descriptor, container_id = struct.unpack(">HI", header)
    return container_id if packet_descriptor == FW_PACKET_DP else None


def has_boson_fdp_signature(path: Path) -> bool:
    """Verify the standard-FDP packet descriptor and Boson container ID."""

    return read_fdp_container_id(path) == BOSON_CONTAINER_ID


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
        raise SystemExit(f"no {NUM_PIXELS}-element Boson pixel array found in decoded data product")
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
    return dt.datetime.fromtimestamp(seconds + micros / 1_000_000, dt.timezone.utc).isoformat()


def _stats(values: list[int | None]) -> tuple[int | None, int | None, float | None]:
    valid = [value for value in values if value is not None]
    if not valid:
        return None, None, None
    return min(valid), max(valid), statistics.fmean(valid)


def write_csv(path: Path, pixels: list[int | None]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        handle.write("# FORMAT,fprime-fdp\n")
        handle.write("# UNITS,raw_counts\n")
        handle.write(f"# WIDTH,{WIDTH}\n")
        handle.write(f"# HEIGHT,{HEIGHT}\n")
        for row in range(HEIGHT):
            offset = row * WIDTH
            handle.write(",".join("NaN" if value is None else str(value) for value in pixels[offset : offset + WIDTH]))
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


def write_fallback_png(path: Path, pixels: list[int | None], scale: int = 2) -> None:
    valid = [value for value in pixels if value is not None]
    minimum = min(valid) if valid else 0
    maximum = max(valid) if valid else 1
    span = max(maximum - minimum, 1)
    rows: list[bytes] = []
    for row in range(HEIGHT):
        out = bytearray()
        offset = row * WIDTH
        for value in pixels[offset : offset + WIDTH]:
            color = b"\xff\xff\xff" if value is None else bytes(hot_color((value - minimum) / span))
            out.extend(color * scale)
        for _ in range(scale):
            rows.append(bytes(out))

    png_width = WIDTH * scale
    png_height = HEIGHT * scale
    scanlines = b"".join(b"\x00" + row for row in rows)
    ihdr = struct.pack(">IIBBBBB", png_width, png_height, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + write_png_chunk(b"IHDR", ihdr)
        + write_png_chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + write_png_chunk(b"IEND", b"")
    )


def open_image(path: Path) -> None:
    if sys.platform == "darwin":
        subprocess.Popen(["open", str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif os.name == "nt":
        os.startfile(path)  # type: ignore[attr-defined]
    else:
        opener = shutil.which("xdg-open") or shutil.which("wslview")
        if opener:
            subprocess.Popen([opener, str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def write_png(path: Path, pixels: list[int | None], *, no_show: bool = True) -> None:
    try:
        import matplotlib.pyplot as plt  # type: ignore
    except ImportError:
        write_fallback_png(path, pixels)
        if not no_show:
            open_image(path)
        return

    grid = [
        [float("nan") if value is None else value for value in pixels[row * WIDTH : (row + 1) * WIDTH]]
        for row in range(HEIGHT)
    ]
    fig, ax = plt.subplots(figsize=(8, 6))
    cmap = plt.get_cmap("hot").copy()
    cmap.set_bad(color="white")
    image = ax.imshow(grid, cmap=cmap, aspect="equal", interpolation="nearest")
    fig.colorbar(image, ax=ax, label="Raw counts (U16)")
    ax.set_title("Boson 320 raw counts")
    ax.set_xlabel("Column")
    ax.set_ylabel("Row")
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    if not no_show:
        plt.show()
    plt.close(fig)


def _write_partial_outputs(
    outdir: Path,
    pixels: list[int | None],
    missing_packet_indices: list[int],
    *,
    no_show: bool,
) -> dict[str, Any]:
    outdir.mkdir(parents=True, exist_ok=True)
    out_json = outdir / "payload.json"
    out_csv = outdir / "payload.csv"
    out_png = outdir / "payload.png"
    minimum, maximum, mean = _stats(pixels)
    payload = {
        "format": "fdp",
        "product": "boson",
        "container_id": BOSON_CONTAINER_ID,
        "partial": True,
        "width": WIDTH,
        "height": HEIGHT,
        "units": "raw_counts",
        "pixels_raw_counts": pixels,
        "unknown_pixels": sum(value is None for value in pixels),
        "missing_packet_indices": sorted(missing_packet_indices),
        "min_raw_counts": minimum,
        "max_raw_counts": maximum,
        "mean_raw_counts": round(mean, 3) if mean is not None else None,
    }
    out_json.write_text(json.dumps(payload, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    write_csv(out_csv, pixels)
    write_png(out_png, pixels, no_show=no_show)
    valid_pixels = sum(value is not None for value in pixels)
    return {
        "input": None,
        "dictionary": None,
        "json": str(out_json),
        "csv": str(out_csv),
        "png": str(out_png),
        "product": "boson",
        "format": "fdp",
        "container_id": BOSON_CONTAINER_ID,
        "units": "raw_counts",
        "width": WIDTH,
        "height": HEIGHT,
        "pixels": NUM_PIXELS,
        "valid_pixels": valid_pixels,
        "missing_pixels": NUM_PIXELS - valid_pixels,
        "min_raw_counts": minimum,
        "max_raw_counts": maximum,
        "mean_raw_counts": round(mean, 3) if mean is not None else None,
        "received_percent": round(100.0 * valid_pixels / NUM_PIXELS, 3),
        "partial": True,
        "missing_packet_indices": sorted(missing_packet_indices),
    }


def decode_partial_product(
    bin_file: Path,
    outdir: Path,
    missing_packet_indices: list[int] | tuple[int, ...],
    packet_data_bytes: int,
    *,
    no_show: bool = True,
) -> dict[str, Any]:
    """Decode a positional Boson FDP while marking missing packet bytes unknown."""

    if not (1 <= int(packet_data_bytes) <= 255):
        raise ValueError("packet_data_bytes must be positive")
    blob = Path(bin_file).read_bytes()
    if not has_boson_fdp_signature(Path(bin_file)):
        raise ValueError("partial Boson product does not have the standard Boson FDP signature")
    if len(blob) < PIXEL_DATA_OFFSET + PIXEL_BYTES:
        raise ValueError(f"partial Boson product has unsafe size {len(blob)}")
    missing = {int(index) for index in missing_packet_indices}
    if any(index < 0 for index in missing):
        raise ValueError("missing packet indices must be non-negative")
    pixels: list[int | None] = []
    for pixel_index in range(NUM_PIXELS):
        offset = PIXEL_DATA_OFFSET + pixel_index * 2
        source_packets = {offset // packet_data_bytes, (offset + 1) // packet_data_bytes}
        if source_packets & missing:
            pixels.append(None)
        else:
            pixels.append(struct.unpack_from(">H", blob, offset)[0])
    summary = _write_partial_outputs(
        Path(outdir).resolve(),
        pixels,
        sorted(missing),
        no_show=no_show,
    )
    summary["input"] = str(Path(bin_file).resolve())
    return summary


def decode_product(args: argparse.Namespace) -> dict[str, Any]:
    bin_file = Path(args.bin_file).resolve()
    if not bin_file.exists():
        raise FileNotFoundError(bin_file)
    if not has_boson_fdp_signature(bin_file):
        raise SystemExit("input is not a standard Boson FDP")

    dictionary = args.dictionary or find_dictionary(Path(__file__).resolve())
    if dictionary is None or not dictionary.exists():
        raise SystemExit("dictionary not found; pass --dictionary <path>")
    outdir = Path(args.outdir).resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    out_json = outdir / "payload.json"
    run_fprime_dp_decode(bin_file, dictionary, out_json)
    decoded = json.loads(out_json.read_text(encoding="utf-8"))
    pixels = extract_pixels(decoded)
    captured_at = find_captured_at(decoded)
    out_csv = outdir / "payload.csv"
    out_png = outdir / "payload.png"
    write_csv(out_csv, pixels)
    png_written = False
    if not getattr(args, "no_png", False):
        write_png(out_png, pixels, no_show=bool(getattr(args, "no_show", True)))
        png_written = True
    minimum, maximum, mean = _stats(pixels)
    return {
        "input": str(bin_file),
        "dictionary": str(Path(dictionary).resolve()),
        "json": str(out_json),
        "csv": str(out_csv),
        "png": str(out_png) if png_written else None,
        "product": "boson",
        "format": "fdp",
        "container_id": BOSON_CONTAINER_ID,
        "units": "raw_counts",
        "width": WIDTH,
        "height": HEIGHT,
        "pixels": len(pixels),
        "captured_at": captured_at,
        "first_raw_counts": pixels[0],
        "center_raw_counts": pixels[(HEIGHT // 2) * WIDTH + (WIDTH // 2)],
        "min_raw_counts": minimum,
        "max_raw_counts": maximum,
        "mean_raw_counts": round(mean, 2) if mean is not None else None,
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bin_file", type=Path, help="Path to a standard Boson .fdp data product")
    parser.add_argument("--dictionary", type=Path, help="Deployment topology dictionary JSON")
    parser.add_argument("--outdir", type=Path, default=Path("./data"))
    parser.add_argument("--no-png", action="store_true", help="Skip PNG generation")
    parser.add_argument("--no-show", action="store_true", help="Save PNG without opening a window")
    parser.add_argument("--summary", action="store_true", help="Print compact JSON summary")
    args = parser.parse_args(argv)
    try:
        summary = decode_product(args)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    if args.summary:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print(f"Wrote JSON: {summary['json']}")
        print(f"Wrote CSV : {summary['csv']}")
        if summary["png"]:
            print(f"Wrote PNG : {summary['png']}")
        else:
            print("PNG skipped")
        print(
            f"Frame: {summary['width']}x{summary['height']} "
            f"{summary['min_raw_counts']}..{summary['max_raw_counts']} raw counts"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
