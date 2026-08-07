#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKETCH_DIR="$ROOT_DIR/firmware/satellite_teensy"
FQBN="teensy:avr:teensy41"
RF_PROFILE="${RF_ENDPOINT_PROFILE:-n2-spacecraft-a}"

export ARDUINO_CONFIG_FILE="$ROOT_DIR/tools/arduino-cli/arduino-cli.yaml"

usage() {
  cat <<'EOF'
Usage: upload.sh [--profile <endpoint-profile>] [usb:<upload-id>]

Upload a previously built satellite endpoint artifact.
Default profile: n2-spacecraft-a
EOF
}

REQUESTED_PORT=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      RF_PROFILE="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --*)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ -n "$REQUESTED_PORT" ]]; then
        echo "Only one upload ID may be supplied" >&2
        exit 2
      fi
      REQUESTED_PORT="$1"
      shift
      ;;
  esac
done

case "$RF_PROFILE" in
  *-spacecraft|*-spacecraft-*) ;;
  *)
    echo "Satellite firmware requires a spacecraft endpoint profile, got: $RF_PROFILE" >&2
    exit 2
    ;;
esac

BUILD_DIR="$ROOT_DIR/build/arduino-cli/$RF_PROFILE"
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

For the current HIL bench, satellite is usually usb:2100000.
EOF
  exit 2
fi

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Missing satellite build for profile $RF_PROFILE: $BUILD_DIR" >&2
  echo "Run: ./tools/arduino-cli/build.sh --profile $RF_PROFILE" >&2
  exit 2
fi

echo "Uploading satellite Teensy RF profile: $RF_PROFILE"
arduino-cli upload --fqbn "$FQBN" -p "$PORT" --input-dir "$BUILD_DIR" "$SKETCH_DIR"
