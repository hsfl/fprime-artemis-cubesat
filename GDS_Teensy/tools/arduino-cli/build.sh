#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKETCH_DIR="$ROOT_DIR/firmware/gds_teensy"
FQBN="teensy:avr:teensy41"
BUILD_DIR="$ROOT_DIR/build/arduino-cli"

export ARDUINO_CONFIG_FILE="$ROOT_DIR/tools/arduino-cli/arduino-cli.yaml"

arduino-cli core update-index
arduino-cli core install teensy:avr
arduino-cli compile \
  --fqbn "$FQBN" \
  --build-path "$BUILD_DIR" \
  "$SKETCH_DIR"
