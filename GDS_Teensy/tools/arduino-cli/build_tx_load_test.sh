#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKETCH_DIR="$ROOT_DIR/firmware/gds_teensy"
FQBN="teensy:avr:teensy41:usb=serial3"
BUILD_DIR="$ROOT_DIR/build/arduino-cli-tx-load-test"
REPO_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
LIBRARIES_DIR="$REPO_ROOT/ArtemisTeensy_N2_Baremetal/firmware/libs"

export ARDUINO_CONFIG_FILE="$ROOT_DIR/tools/arduino-cli/arduino-cli.yaml"

arduino-cli core update-index
arduino-cli core install teensy:avr
CPP_FLAGS="$(
  arduino-cli compile --fqbn "$FQBN" --show-properties=expanded "$SKETCH_DIR" |
    sed -n 's/^build\.flags\.cpp=//p'
)"
arduino-cli compile \
  --fqbn "$FQBN" \
  --build-property "build.flags.cpp=$CPP_FLAGS -DGDS_TX_LOAD_TEST=1" \
  --libraries "$LIBRARIES_DIR" \
  --build-path "$BUILD_DIR" \
  "$SKETCH_DIR"

LOAD_TEST_ELF="$BUILD_DIR/gds_teensy.ino.elf"
if [[ ! -f "$LOAD_TEST_ELF" ]] || ! grep -aFq "[GDS_LOAD] dedicated TX load-test image" "$LOAD_TEST_ELF"; then
  echo "Build completed without the TX load-test marker; refusing the artifact." >&2
  exit 2
fi
