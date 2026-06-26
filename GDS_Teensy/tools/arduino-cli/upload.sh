#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKETCH_DIR="$ROOT_DIR/firmware/gds_teensy"
FQBN="teensy:avr:teensy41:usb=serial3"
BUILD_DIR="$ROOT_DIR/build/arduino-cli"

export ARDUINO_CONFIG_FILE="$ROOT_DIR/tools/arduino-cli/arduino-cli.yaml"

REQUESTED_PORT="${1:-}"
PORT="${REQUESTED_PORT:-/dev/ttyACM0}"

TEENSY_PORTS="$(arduino-cli board list 2>/dev/null | awk '$1 ~ /^usb:/ {print $1}')"
TEENSY_COUNT="$(printf '%s\n' "$TEENSY_PORTS" | sed '/^$/d' | wc -l | tr -d ' ')"

if [[ -z "$REQUESTED_PORT" && "$TEENSY_COUNT" == "1" ]]; then
  PORT="$TEENSY_PORTS"
fi

if [[ "$TEENSY_COUNT" -gt 1 && "$PORT" != usb:* ]]; then
  cat >&2 <<EOF
Multiple Teensy upload ports are connected. Refusing ambiguous serial upload: $PORT

Use the physical Teensy upload ID from:
  arduino-cli board list

Current Teensy upload ports:
$TEENSY_PORTS

For the current HIL bench, ground is usually usb:100000.
EOF
  exit 2
fi

arduino-cli upload --fqbn "$FQBN" -p "$PORT" --input-dir "$BUILD_DIR" "$SKETCH_DIR"
