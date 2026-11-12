#!/usr/bin/env python3
"""Read-only test oracle inside Pi storage, used only after ground delivery."""
import argparse
import hashlib
import json
from pathlib import Path

from common import crc16


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/payload"))
    args = parser.parse_args()
    paths = [path for path in args.root.rglob("*.fdp") if path.is_file()]
    if not paths:
        raise SystemExit("No persisted Pi data product")
    latest = max(paths, key=lambda path: path.stat().st_mtime_ns)
    blob = latest.read_bytes()
    print(json.dumps({"path": str(latest.relative_to(args.root)), "bytes": len(blob),
                      "crc16": crc16(blob), "sha256": hashlib.sha256(blob).hexdigest()}))
