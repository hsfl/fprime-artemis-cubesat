#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_ACTIVATE="$ROOT_DIR/fprime-venv/bin/activate"
DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
DICT_BASENAME="${DEPLOYMENT_NAME}TopologyDictionary.json"

PORT="${GDS_UART_PORT:-}"
BAUD="115200"
GUI_PORT="5050"
FRAMING="space-packet-space-data-link"
DICT_PATH="${GDS_DICTIONARY:-}"
DRY_RUN="false"

usage() {
  cat <<'EOF'
Usage: run_gds_uart.sh [options]

Run fprime-gds in UART mode for the ArtemisRpiTeensyDeployment.

Options:
  --port <path>         UART device path (or set GDS_UART_PORT)
  --baud <rate>         UART baud rate (default: 115200)
  --gui-port <port>     GDS web UI port (default: 5050)
  --framing <mode>      GDS framing mode (default: space-packet-space-data-link)
  --dictionary <path>   Path to deployment dictionary JSON (or set GDS_DICTIONARY)
  --dry-run             Print the resolved fprime-gds command and exit
  -h, --help            Show this help text
EOF
}

detect_uart_port() {
  local matches=()
  local pattern
  for pattern in /dev/cu.usbmodem* /dev/ttyACM* /dev/ttyUSB*; do
    for candidate in $pattern; do
      [[ -e "$candidate" ]] || continue
      matches+=("$candidate")
    done
  done

  if [[ "${#matches[@]}" -eq 1 ]]; then
    PORT="${matches[0]}"
    return 0
  fi

  if [[ "${#matches[@]}" -gt 1 ]]; then
    {
      echo "Multiple serial devices found; pass the ground Teensy data port explicitly:"
      printf '  %s\n' "${matches[@]}"
    } >&2
  else
    echo "No supported serial device found. Pass --port <device>." >&2
  fi
  return 1
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
    --framing)
      FRAMING="${2:-}"
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

if [[ -z "$PORT" ]]; then
  detect_uart_port || exit 1
fi

if [[ -z "$DICT_PATH" || ! -f "$DICT_PATH" ]]; then
  AUTO_DICT="$(find "$ROOT_DIR/build-artifacts" -type f -path "*/${DEPLOYMENT_NAME}/dict/${DICT_BASENAME}" | sort | head -1 || true)"
  if [[ -n "${AUTO_DICT}" ]]; then
    DICT_PATH="$AUTO_DICT"
  else
    if [[ -n "$DICT_PATH" ]]; then
      echo "Dictionary not found at: $DICT_PATH" >&2
    else
      echo "Dictionary not found under: $ROOT_DIR/build-artifacts" >&2
    fi
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
  --framing-selection "$FRAMING"
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
echo "  framing:    $FRAMING"
exec "${CMD[@]}"
