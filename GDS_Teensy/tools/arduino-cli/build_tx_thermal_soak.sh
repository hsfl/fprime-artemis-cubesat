#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKETCH_DIR="$ROOT_DIR/firmware/gds_tx_thermal_soak"
FQBN="teensy:avr:teensy41:usb=serial2"
POWER_CODE="${1:-7}"
if [[ ! "$POWER_CODE" =~ ^[0-7]$ ]]; then
  echo "Power code must be an integer from 0 through 7." >&2
  exit 2
fi
BUILD_DIR="$ROOT_DIR/build/arduino-cli-tx-thermal-soak-power-$POWER_CODE"
REPO_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
LIBRARIES_DIR="$REPO_ROOT/ArtemisTeensy_N2_Baremetal/firmware/libs"

export ARDUINO_CONFIG_FILE="$ROOT_DIR/tools/arduino-cli/arduino-cli.yaml"

arduino-cli core update-index
arduino-cli core install teensy:avr
arduino-cli compile \
  --fqbn "$FQBN" \
  --build-property "build.flags.defs=-D__IMXRT1062__ -DTEENSYDUINO=160 -DGDS_TX_SOAK_POWER_CODE=$POWER_CODE" \
  --libraries "$LIBRARIES_DIR" \
  --build-path "$BUILD_DIR" \
  "$SKETCH_DIR"

SOAK_ELF="$BUILD_DIR/gds_tx_thermal_soak.ino.elf"
if [[ ! -f "$SOAK_ELF" ]] || ! grep -aFq "[GDS_TX_SOAK] standalone autonomous TX-only thermal-soak image" "$SOAK_ELF" || ! (set +o pipefail; strings "$SOAK_ELF" | grep -Fxq "GDS_TX_SOAK_POWER_CODE=$POWER_CODE"); then
  echo "Build completed without the TX thermal-soak marker; refusing the artifact." >&2
  exit 2
fi
