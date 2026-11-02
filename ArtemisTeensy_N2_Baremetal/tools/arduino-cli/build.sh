#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

if [[ "$(uname -s)" == "Darwin" && "$(sysctl -n hw.optional.arm64 2>/dev/null || true)" == "1" ]]; then
  if [[ -x /opt/homebrew/bin/arduino-cli ]]; then
    ARDUINO_CLI=(/usr/bin/arch -arm64 /opt/homebrew/bin/arduino-cli)
  else
    ARDUINO_CLI_BIN="$(command -v arduino-cli)"
    ARDUINO_CLI=(/usr/bin/arch -arm64 "$ARDUINO_CLI_BIN")
  fi
else
  ARDUINO_CLI_BIN="$(command -v arduino-cli)"
  ARDUINO_CLI=("$ARDUINO_CLI_BIN")
fi
SKETCH_DIR="$ROOT_DIR/firmware/satellite_teensy"
FQBN="teensy:avr:teensy41"
LIBRARIES_DIR="$ROOT_DIR/firmware/libs"
PROFILE="${1:-cdr}"

case "$PROFILE" in
  cdr)
    PROFILE_MARKER="CDR_NO_RADIO"
    BUILD_DIR="$ROOT_DIR/build/arduino-cli"
    ;;
  legacy-rf)
    PROFILE_MARKER="LEGACY_RFM23BP"
    BUILD_DIR="$ROOT_DIR/build/arduino-cli-legacy-rf"
    ;;
  *)
    echo "Usage: $0 [cdr|legacy-rf]" >&2
    exit 2
    ;;
esac

export ARDUINO_CONFIG_FILE="$ROOT_DIR/tools/arduino-cli/arduino-cli.yaml"

"${ARDUINO_CLI[@]}" core update-index
"${ARDUINO_CLI[@]}" core install teensy:avr

BUILD_DEFS="$("${ARDUINO_CLI[@]}" compile --show-properties \
  --fqbn "$FQBN" \
  --libraries "$LIBRARIES_DIR" \
  "$SKETCH_DIR" | sed -n 's/^build\.flags\.defs=//p' | head -n 1)"
if [[ -z "$BUILD_DEFS" ]]; then
  echo "Unable to read Teensy board compile definitions" >&2
  exit 1
fi
if [[ "$PROFILE" == "legacy-rf" ]]; then
  BUILD_DEFS="$BUILD_DEFS -DARTEMIS_CDR_LEGACY_RF=1"
fi
BUILD_PROFILE_ARGS=(--build-property "build.flags.defs=$BUILD_DEFS")

mkdir -p "$BUILD_DIR"
rm -f "$BUILD_DIR/firmware-profile.txt"
"${ARDUINO_CLI[@]}" compile --clean \
  "${BUILD_PROFILE_ARGS[@]}" \
  --fqbn "$FQBN" \
  --libraries "$LIBRARIES_DIR" \
  --build-path "$BUILD_DIR" \
  "$SKETCH_DIR"

SOURCE_COMMIT="$(git -C "$ROOT_DIR" rev-parse HEAD)"
cat > "$BUILD_DIR/firmware-profile.txt" <<EOF
profile=$PROFILE_MARKER
fqbn=$FQBN
source_commit=$SOURCE_COMMIT
EOF
echo "Wrote $BUILD_DIR/firmware-profile.txt"
