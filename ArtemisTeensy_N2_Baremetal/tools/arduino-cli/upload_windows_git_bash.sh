#!/usr/bin/env bash
set -euo pipefail

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;
  *)
    cat >&2 <<'EOF'
This helper is for Git Bash on Windows.

Use ./tools/arduino-cli/upload.sh on macOS, Linux, or WSL.
EOF
    exit 2
    ;;
esac

if ! command -v arduino-cli >/dev/null 2>&1; then
  cat >&2 <<'EOF'
arduino-cli was not found on PATH.

Install Arduino IDE 2.x or add Arduino CLI to your Git Bash PATH.
EOF
  exit 127
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKETCH_DIR="$ROOT_DIR/firmware/satellite_teensy"
FQBN="teensy:avr:teensy41"
BUILD_DIR="$ROOT_DIR/build/arduino-cli-windows"
CONFIG_DIR="$ROOT_DIR/build/arduino-cli-windows-config"
CONFIG_FILE="$CONFIG_DIR/arduino-cli.windows.generated.yaml"

LOCAL_APPDATA_WIN="$(cygpath -m "${LOCALAPPDATA:-$HOME/AppData/Local}")"
ARDUINO_DATA_DIR="${ARDUINO_WINDOWS_DATA_DIR:-$LOCAL_APPDATA_WIN/Arduino15-n2}"
mkdir -p "$CONFIG_DIR"
cat > "$CONFIG_FILE" <<EOF
board_manager:
  additional_urls:
    - https://www.pjrc.com/teensy/package_teensy_index.json
directories:
  data: $ARDUINO_DATA_DIR
  downloads: $ARDUINO_DATA_DIR/staging
  user: $ARDUINO_DATA_DIR/sketchbook
EOF
export ARDUINO_CONFIG_FILE="$CONFIG_FILE"

REQUESTED_PORT="${1:-}"

TEENSY_PORTS="$(arduino-cli board list 2>/dev/null | awk '$1 ~ /^usb:/ {print $1}')"
TEENSY_COUNT="$(printf '%s\n' "$TEENSY_PORTS" | sed '/^$/d' | wc -l | tr -d ' ')"

if [[ -n "$REQUESTED_PORT" ]]; then
  PORT="$REQUESTED_PORT"
elif [[ "$TEENSY_COUNT" == "1" ]]; then
  PORT="$TEENSY_PORTS"
else
  cat >&2 <<EOF
No Teensy upload ID was provided, and auto-targeting is not safe.

Use the physical Teensy upload ID from:
  arduino-cli board list

Current Teensy upload ports:
$TEENSY_PORTS

For the current HIL bench, satellite is usually usb:2100000, but verify the ID
on this Windows host before uploading.
EOF
  exit 2
fi

if [[ "$TEENSY_COUNT" -gt 1 && "$PORT" != usb:* ]]; then
  cat >&2 <<EOF
Multiple Teensy upload ports are connected. Refusing ambiguous upload: $PORT

Use the physical Teensy upload ID from:
  arduino-cli board list

Current Teensy upload ports:
$TEENSY_PORTS
EOF
  exit 2
fi

arduino-cli upload --fqbn "$FQBN" -p "$PORT" --input-dir "$BUILD_DIR" "$SKETCH_DIR"
