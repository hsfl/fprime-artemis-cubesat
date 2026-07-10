#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-usb:2100000}"

if [[ "$PORT" != usb:* ]]; then
  echo "Refusing non-physical upload target: $PORT" >&2
  echo "Pass the satellite Teensy physical ID, normally usb:2100000." >&2
  exit 2
fi

arduino-cli upload \
  --fqbn teensy:avr:teensy41 \
  --port "$PORT" \
  --input-file "$ROOT/artifacts/teensy-satellite/satellite_teensy.ino.hex"
