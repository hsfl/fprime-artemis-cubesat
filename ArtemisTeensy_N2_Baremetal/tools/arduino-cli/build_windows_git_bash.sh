#!/usr/bin/env bash
set -euo pipefail

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;
  *)
    cat >&2 <<'EOF'
This helper is for Git Bash on Windows.

Use ./tools/arduino-cli/build.sh on macOS, Linux, or WSL.
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
LIBRARIES_DIR="$ROOT_DIR/firmware/libs"
CONFIG_DIR="$ROOT_DIR/build/arduino-cli-windows-config"
CONFIG_FILE="$CONFIG_DIR/arduino-cli.windows.generated.yaml"

# Keep the Teensy toolchain in a short path. The Windows Teensy GCC package can
# fail to find its own C++ headers when installed under this repo's deep path.
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

arduino-cli core update-index
arduino-cli core install teensy:avr
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries "$LIBRARIES_DIR" \
  --build-path "$BUILD_DIR" \
  "$SKETCH_DIR"
