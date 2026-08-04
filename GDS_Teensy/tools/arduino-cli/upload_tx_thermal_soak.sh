#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKETCH_DIR="$ROOT_DIR/firmware/gds_tx_thermal_soak"
FQBN="teensy:avr:teensy41:usb=serial2"
if [[ "${1:-}" == usb:* ]]; then
  POWER_CODE="7"
  REQUESTED_PORT="$1"
else
  POWER_CODE="${1:-7}"
  REQUESTED_PORT="${2:-}"
fi
if [[ ! "$POWER_CODE" =~ ^[0-7]$ ]]; then
  echo "Power code must be an integer from 0 through 7." >&2
  exit 2
fi
BUILD_DIR="$ROOT_DIR/build/arduino-cli-tx-thermal-soak-power-$POWER_CODE"
SOAK_ELF="$BUILD_DIR/gds_tx_thermal_soak.ino.elf"

export ARDUINO_CONFIG_FILE="$ROOT_DIR/tools/arduino-cli/arduino-cli.yaml"

if [[ ! -f "$SOAK_ELF" ]] || ! grep -aFq "[GDS_TX_SOAK] standalone autonomous TX-only thermal-soak image" "$SOAK_ELF" || ! (set +o pipefail; strings "$SOAK_ELF" | grep -Fxq "GDS_TX_SOAK_POWER_CODE=$POWER_CODE"); then
  echo "TX thermal-soak build not found or is invalid. Run ./tools/arduino-cli/build_tx_thermal_soak.sh first." >&2
  exit 2
fi

TEENSY_PORTS="$(arduino-cli board list 2>/dev/null | awk '$1 ~ /^usb:/ {print $1}')"
TEENSY_COUNT="$(printf '%s\n' "$TEENSY_PORTS" | sed '/^$/d' | wc -l | tr -d ' ')"

if [[ -z "$REQUESTED_PORT" ]]; then
  if [[ "$TEENSY_COUNT" != "1" ]]; then
  echo "Specify the physical Teensy upload ID (for example, usb:100000)." >&2
    exit 2
  fi
  REQUESTED_PORT="$TEENSY_PORTS"
fi

if [[ "$REQUESTED_PORT" != usb:* ]]; then
  echo "Refusing serial-path upload. Use the physical usb:* Teensy ID." >&2
  exit 2
fi

arduino-cli upload --fqbn "$FQBN" -p "$REQUESTED_PORT" --input-dir "$BUILD_DIR" "$SKETCH_DIR"
