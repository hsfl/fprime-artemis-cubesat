#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
VENV_ACTIVATE="$ROOT_DIR/fprime-venv/bin/activate"
DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
DICT_BASENAME="${DEPLOYMENT_NAME}TopologyDictionary.json"

PI_HOST="${PI_HOST:-artemis-pi}"
PI_SERVICE="${PI_SERVICE:-artemis-fprime.service}"
GDS_DATA_PORT="${GDS_DATA_PORT:-}"
GDS_DEBUG_PORT="${GDS_DEBUG_PORT:-}"
SAT_DEBUG_PORT="${SAT_DEBUG_PORT:-}"
UART_BAUD="${UART_BAUD:-115200}"
GUI_PORT="${GUI_PORT:-5051}"
TOKEN="${TOKEN:-$(( (RANDOM % 9000) + 1000 ))}"
START_GDS="false"
NO_COMMAND="false"
DICT_PATH="${DICT_PATH:-$ROOT_DIR/build-artifacts/pi-zero-w-armv6hf/${DEPLOYMENT_NAME}/dict/${DICT_BASENAME}}"

usage() {
  cat <<'EOF'
Usage: demo_rf_mvp_smoke.sh [options]

Checks the RF MVP demo setup and optionally sends missionApp.PING through
the active fprime-gds UART session.

Options:
  --start-gds           Start fprime-gds in the background before sending PING
  --no-command          Only check ports/Pi service/dictionary; do not send PING
  --token <int>         PING token to send (default: random 1000-9999)
  --gui-port <port>     GDS GUI port when --start-gds is used (default: 5051)
  --dictionary <path>   Dictionary JSON path (default: Pi Zero W cross-build dict)
  --pi-host <host>      SSH host/alias for Pi (default: artemis-pi)
  --gds-data-port <dev> Ground Teensy channel 0 data serial device
  --gds-debug-port <dev> Ground Teensy debug serial device
  --sat-debug-port <dev> Satellite Teensy debug serial device
  -h, --help            Show this help text

Expected proof of success:
  MissionApp pong token=<token>
  Opcode 0x10006001 dispatched
  Opcode 0x10006001 completed
EOF
}

log() {
  printf '[demo-smoke] %s\n' "$*"
}

fail() {
  printf '[demo-smoke] ERROR: %s\n' "$*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --start-gds)
      START_GDS="true"
      shift
      ;;
    --no-command)
      NO_COMMAND="true"
      shift
      ;;
    --token)
      TOKEN="${2:-}"
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
    --pi-host)
      PI_HOST="${2:-}"
      shift 2
      ;;
    --gds-data-port)
      GDS_DATA_PORT="${2:-}"
      shift 2
      ;;
    --gds-debug-port)
      GDS_DEBUG_PORT="${2:-}"
      shift 2
      ;;
    --sat-debug-port)
      SAT_DEBUG_PORT="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "Unknown argument: $1"
      ;;
  esac
done

[[ -f "$VENV_ACTIVATE" ]] || fail "Missing venv: $VENV_ACTIVATE"
[[ -f "$DICT_PATH" ]] || fail "Missing dictionary: $DICT_PATH"
[[ -n "$GDS_DATA_PORT" ]] || fail "Set --gds-data-port or GDS_DATA_PORT"
[[ -n "$GDS_DEBUG_PORT" ]] || fail "Set --gds-debug-port or GDS_DEBUG_PORT"
[[ -n "$SAT_DEBUG_PORT" ]] || fail "Set --sat-debug-port or SAT_DEBUG_PORT"
[[ -e "$GDS_DATA_PORT" ]] || fail "Missing ground data port: $GDS_DATA_PORT"
[[ -e "$GDS_DEBUG_PORT" ]] || fail "Missing ground debug port: $GDS_DEBUG_PORT"
[[ -e "$SAT_DEBUG_PORT" ]] || fail "Missing satellite debug port: $SAT_DEBUG_PORT"

log "ports present"
log "  ground data: $GDS_DATA_PORT"
log "  ground debug: $GDS_DEBUG_PORT"
log "  satellite debug: $SAT_DEBUG_PORT"
log "dictionary: $DICT_PATH"

log "checking Pi service: $PI_HOST / $PI_SERVICE"
PI_ACTIVE="$(ssh "$PI_HOST" "systemctl is-active '$PI_SERVICE'")"
[[ "$PI_ACTIVE" == "active" ]] || fail "Pi service is not active: $PI_ACTIVE"
log "Pi service active"

if [[ "$START_GDS" == "true" ]]; then
  mkdir -p "$ROOT_DIR/tools/logs"
  GDS_LOG="$ROOT_DIR/tools/logs/demo_gds_$(date +%Y%m%d_%H%M%S).log"
  log "starting fprime-gds in background; log: $GDS_LOG"
  (
    cd "$ROOT_DIR"
    # shellcheck disable=SC1090
    . "$VENV_ACTIVATE"
    exec fprime-gds -n \
      --communication-selection uart \
      --uart-device "$GDS_DATA_PORT" \
      --uart-baud "$UART_BAUD" \
      --framing-selection space-packet-space-data-link \
      --dictionary "$DICT_PATH" \
      --gui-port "$GUI_PORT" \
      --log-to-stdout \
      --log-level-gds INFO
  ) >"$GDS_LOG" 2>&1 &
  GDS_PID="$!"
  log "fprime-gds pid: $GDS_PID, gui: http://localhost:$GUI_PORT"
  sleep 4
fi

if [[ "$NO_COMMAND" == "true" ]]; then
  log "no-command mode complete"
  exit 0
fi

# shellcheck disable=SC1090
. "$VENV_ACTIVATE"

log "sending missionApp.PING token=$TOKEN"
PING_CMD=(
  fprime-cli command-send
  "${DEPLOYMENT_NAME}.missionApp.PING"
  --arguments "$TOKEN"
  --dictionary "$DICT_PATH"
  --log-level-gds ERROR
)
if command -v timeout >/dev/null 2>&1; then
  timeout 20s "${PING_CMD[@]}"
else
  "${PING_CMD[@]}"
fi

log "checking Pi journal for pong"
JOURNAL_OUTPUT="$(ssh "$PI_HOST" "journalctl -u '$PI_SERVICE' --since '45 seconds ago' --no-pager | egrep 'MissionApp|PING|pong|OpCode|completed|ERROR|WARNING' | tail -100")"
printf '%s\n' "$JOURNAL_OUTPUT"

grep -q "MissionApp pong token=$TOKEN" <<<"$JOURNAL_OUTPUT" || fail "pong token=$TOKEN not found in Pi journal"
grep -Eq "Op[Cc]ode 0x10006001 completed" <<<"$JOURNAL_OUTPUT" || fail "PING completion opcode not found in Pi journal"

log "PASS: command path verified with token=$TOKEN"
