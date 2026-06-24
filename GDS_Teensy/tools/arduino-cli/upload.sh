#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKETCH_DIR="$ROOT_DIR/firmware/gds_teensy"
FQBN="teensy:avr:teensy41:usb=serial3"
BUILD_DIR="$ROOT_DIR/build/arduino-cli"

export ARDUINO_CONFIG_FILE="$ROOT_DIR/tools/arduino-cli/arduino-cli.yaml"

PORT="${1:-/dev/ttyACM0}"

arduino-cli upload --fqbn "$FQBN" -p "$PORT" --input-dir "$BUILD_DIR" "$SKETCH_DIR"
