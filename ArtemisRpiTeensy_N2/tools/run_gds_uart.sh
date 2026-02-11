#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_ACTIVATE="$ROOT_DIR/fprime-venv/bin/activate"
DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
DICT_BASENAME="${DEPLOYMENT_NAME}TopologyDictionary.json"

PORT="/dev/cu.usbmodem115551201"
BAUD="115200"
GUI_PORT="5050"
DICT_PATH="${ROOT_DIR}/build-artifacts/Darwin/${DEPLOYMENT_NAME}/dict/${DICT_BASENAME}"
DRY_RUN="false"

usage() {
  cat <<'EOF'
Usage: run_gds_uart.sh [options]

Run fprime-gds in UART mode for the ArtemisRpiTeensyDeployment.

Options:
  --port <path>         UART device path (default: /dev/cu.usbmodem115551201)
  --baud <rate>         UART baud rate (default: 115200)
  --gui-port <port>     GDS web UI port (default: 5050)
  --dictionary <path>   Path to deployment dictionary JSON
  --dry-run             Print the resolved fprime-gds command and exit
  -h, --help            Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      PORT="${2:-}"
      shift 2
      ;;
    --baud)
      BAUD="${2:-}"
      shift 2
      ;;
    --gui-port)
      GUI_PORT="${2:-}"
      shift 2
      ;;
    --dictionary)
      DICT_PATH="${2:-}"
      shift 2
      ;;
    --dry-run)
      DRY_RUN="true"
      shift
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

if [[ ! -f "$VENV_ACTIVATE" ]]; then
  echo "Missing F' virtual environment activate script: $VENV_ACTIVATE" >&2
  exit 1
fi

if [[ ! -f "$DICT_PATH" ]]; then
  AUTO_DICT="$(find "$ROOT_DIR/build-artifacts" -type f -path "*/${DEPLOYMENT_NAME}/dict/${DICT_BASENAME}" | head -1 || true)"
  if [[ -n "${AUTO_DICT}" ]]; then
    DICT_PATH="$AUTO_DICT"
  else
    echo "Dictionary not found at: $DICT_PATH" >&2
    echo "Run from ArtemisRpiTeensy_N2 after build:" >&2
    echo "  fprime-util generate -f && fprime-util build" >&2
    exit 1
  fi
fi

# shellcheck disable=SC1090
. "$VENV_ACTIVATE"

CMD=(
  fprime-gds
  -n
  --dictionary "$DICT_PATH"
  --communication-selection uart
  --uart-device "$PORT"
  --uart-baud "$BAUD"
  --gui-port "$GUI_PORT"
  --framing-selection fprime
)

if [[ "$DRY_RUN" == "true" ]]; then
  printf '%q ' "${CMD[@]}"
  printf '\n'
  exit 0
fi

echo "Launching fprime-gds over UART"
echo "  dictionary: $DICT_PATH"
echo "  port:       $PORT"
echo "  baud:       $BAUD"
echo "  gui-port:   $GUI_PORT"
exec "${CMD[@]}"
