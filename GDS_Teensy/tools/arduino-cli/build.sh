#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKETCH_DIR="$ROOT_DIR/firmware/gds_teensy"
FQBN="teensy:avr:teensy41:usb=serial3"
TEENSY_CORE_VERSION="1.59.0"
REPO_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
LIBRARIES_DIR="$REPO_ROOT/ArtemisTeensy_N2_Baremetal/firmware/libs"
RF_PROFILE="${RF_ENDPOINT_PROFILE:-n2-gds-a}"

usage() {
  cat <<'EOF'
Usage: build.sh [--profile <endpoint-profile>]

Build ground Teensy firmware for a named GDS RF endpoint profile.
Default: n2-gds-a
EOF
}

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
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "$RF_PROFILE" in
  *-gds|*-gds-*) ;;
  *)
    echo "Ground firmware requires a GDS endpoint profile, got: $RF_PROFILE" >&2
    exit 2
    ;;
esac

RF_PROFILE_MACRO="RF_PROFILE_$(printf '%s' "$RF_PROFILE" | tr '[:lower:]-' '[:upper:]_')"
BUILD_DIR="$ROOT_DIR/build/arduino-cli/$RF_PROFILE"

export ARDUINO_CONFIG_FILE="$ROOT_DIR/tools/arduino-cli/arduino-cli.yaml"

arduino-cli core update-index
arduino-cli core install "teensy:avr@$TEENSY_CORE_VERSION"
echo "Building ground Teensy RF profile: $RF_PROFILE"
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries "$LIBRARIES_DIR" \
  --build-property "compiler.cpp.extra_flags=-DRF_ENDPOINT_PROFILE=$RF_PROFILE_MACRO" \
  --build-path "$BUILD_DIR" \
  "$SKETCH_DIR"
