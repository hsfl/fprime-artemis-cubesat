# simple script to view the data products downloaded from rpi
# input: path to the data product file found in ~/DpCat/
# output: decoded json, extracted CSV, and a png image of the thermal data
#
# Pipeline:
#   note: make sure fprime-venv is activated, and numpy/matplotlib are installed in the venv
#   1. shell out to `fprime-dp decode --bin-file <fdp> --dictionary <dict> --output <json>`

#   2. pull the 19,200-pixel thermal array out of the decoded JSON
#   3. reshape to 120x160, convert centi-Kelvin -> degrees C
#   4. write a CSV (compatible with thermal_data_viewer.py) and a PNG
#
# Usage:
#   python dp_lepton_viewer.py <path/to/file.fdp>
#   python dp_lepton_viewer.py <path/tofile.fdp> --dictionary <path/to/Dictionary.json>

import argparse
import glob
import json
import os
import subprocess
import sys

import numpy as np
import matplotlib.pyplot as plt

# Lepton 3.x native frame. Must match LeptonCamera::WIDTH/HEIGHT.
WIDTH = 160
HEIGHT = 120
NUM_PIXELS = WIDTH * HEIGHT  # 19,200


def find_dictionary():
    """Best-effort locate the deployment JSON dictionary under build-artifacts."""
    here = os.path.dirname(os.path.abspath(__file__))
    patterns = [
        os.path.join(here, "build-artifacts", "*", "*", "dict", "*Dictionary.json"),
        os.path.join(here, "build-artifacts", "*", "dict", "*Dictionary.json"),
        os.path.join(here, "**", "*TopologyDictionary.json"),
        os.path.join(here, "**", "*Dictionary.json"),
    ]
    for pat in patterns:
        hits = glob.glob(pat, recursive=True)
        if hits:
            return hits[0]
    return None


def run_fprime_dp_decode(bin_file, dictionary, out_json):
    """Invoke the `fprime-dp decode` ground CLI to turn a .fdp into JSON."""
    cmd = [
        "fprime-dp", "decode",
        "--bin-file", bin_file,
        "--dictionary", dictionary,
        "--output", out_json,
    ]
    print("Running:", " ".join(cmd))
    try:
        subprocess.run(cmd, check=True)
    except FileNotFoundError:
        sys.exit("Error: `fprime-dp` not found on PATH. Activate the fprime venv first.")
    except subprocess.CalledProcessError as e:
        sys.exit(f"Error: fprime-dp decode failed (exit {e.returncode}).")
    if not os.path.exists(out_json):
        sys.exit(f"Error: decode reported success but {out_json} was not written.")
    return out_json


def _as_number(elem):
    """Pixel elements decode as {"value": N, "type": "U16"}; unwrap to N."""
    if isinstance(elem, dict):
        return elem.get("value")
    return elem


def extract_pixels(decoded):
    """Recursively find the 19,200-element thermal frame in the decoded JSON.

    Robust to the exact schema: the pixel array is the only list of length
    NUM_PIXELS. Its elements are dicts ({"value": N, "type": "U16"}) -- the
    fprime-dp decode wraps every primitive -- so we unwrap each to its number.
    """
    def walk(node):
        if isinstance(node, list):
            if len(node) == NUM_PIXELS:
                nums = [_as_number(v) for v in node]
                if all(isinstance(v, (int, float)) for v in nums):
                    return nums
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
        sys.exit(
            f"Error: no {NUM_PIXELS}-element pixel array found in the decoded JSON.\n"
            "Inspect the .json to confirm the record/field layout."
        )
    return np.array(pixels, dtype=np.uint16)


def find_captured_at(decoded):
    """Build an ISO-8601 UTC timestamp from the container header's Time field.

    Header.Time has direct integer seconds/microseconds (base 2 =
    TB_WORKSTATION_TIME = wall-clock unix time). Returns None if absent.
    """
    import datetime

    time_obj = decoded.get("Header", {}).get("Time", {})
    seconds = time_obj.get("seconds")
    micros = time_obj.get("microseconds", 0)
    if not isinstance(seconds, int):
        return None
    dt = datetime.datetime.fromtimestamp(seconds + micros / 1e6, datetime.timezone.utc)
    return dt.isoformat()


def main():
    parser = argparse.ArgumentParser(description="Decode + view a Lepton thermal data product (.fdp)")
    parser.add_argument("bin_file", help="path to the .fdp data product file")
    parser.add_argument("--dictionary", help="path to the deployment JSON dictionary (auto-detected if omitted)")
    parser.add_argument("--outdir", default="./data", help="output directory for .json/.csv/.png (default: cwd)")
    parser.add_argument("--no-show", action="store_true", help="save the PNG but don't open a window")
    args = parser.parse_args()

    if not os.path.exists(args.bin_file):
        sys.exit(f"Error: file not found: {args.bin_file}")

    dictionary = args.dictionary or find_dictionary()
    if not dictionary or not os.path.exists(dictionary):
        sys.exit(
            "Error: dictionary not found. Pass --dictionary <path>.\n"
            "It's the deployment JSON dict under build-artifacts/.../dict/*Dictionary.json"
        )

    os.makedirs(args.outdir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(args.bin_file))[0]
    out_json = os.path.join(args.outdir, stem + ".json")
    out_csv = os.path.join(args.outdir, stem + ".csv")
    out_png = os.path.join(args.outdir, stem + ".png")

    # 1. decode .fdp -> json via the ground CLI
    run_fprime_dp_decode(args.bin_file, dictionary, out_json)
    with open(out_json) as f:
        decoded = json.load(f)

    # 2. extract + reshape the frame
    raw = extract_pixels(decoded)
    grid_raw = raw.reshape((HEIGHT, WIDTH))

    # 3. centi-Kelvin -> degrees C (Lepton outputs Kelvin*100)
    grid_c = grid_raw.astype(np.float64) / 100.0 - 273.15

    captured_at = find_captured_at(decoded)

    # 4a. CSV (thermal_data_viewer.py-compatible: optional # metadata then grid)
    # TODO: add GPS and IMU data if present in the record
    with open(out_csv, "w") as f:
        if captured_at:
            f.write(f"# CAPTURED_AT,{captured_at}\n")
        np.savetxt(f, grid_c, delimiter=",", fmt="%.2f")
    print(f"Wrote CSV : {out_csv}")

    # 4b. PNG
    fig, ax = plt.subplots(figsize=(8, 6))
    im = ax.imshow(grid_c, cmap="hot", aspect="equal", interpolation="nearest")
    fig.colorbar(im, ax=ax, label="Temperature (C)")
    ax.set_title(f"{stem}\nmin {grid_c.min():.1f}C  max {grid_c.max():.1f}C  mean {grid_c.mean():.1f}C")
    ax.set_xlabel("Column")
    ax.set_ylabel("Row")
    fig.tight_layout()
    fig.savefig(out_png, dpi=120)
    print(f"Wrote PNG : {out_png}")
    print(f"Frame: {WIDTH}x{HEIGHT}  range {grid_c.min():.1f}..{grid_c.max():.1f} C  mean {grid_c.mean():.1f} C")

    if not args.no_show:
        plt.show()


if __name__ == "__main__":
    main()
