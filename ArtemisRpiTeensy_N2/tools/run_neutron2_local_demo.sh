#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
VENV_ACTIVATE="$ROOT_DIR/fprime-venv/bin/activate"
DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
DICT_BASENAME="${DEPLOYMENT_NAME}TopologyDictionary.json"
CAPTURE_DIR="${CAPTURE_DIR:-/tmp/neutron_payload_captures}"
GUI_PORT="${GUI_PORT:-5050}"
VIEWER_PORT="${VIEWER_PORT:-8062}"
DELAY_SECONDS="${DELAY_SECONDS:-10}"
CAPTURE_SECONDS="${CAPTURE_SECONDS:-10}"
HOLD_AFTER_SEQUENCE="true"
DICT_PATH="${DICT_PATH:-}"
TOPOLOGY_PROFILE="${NEUTRON2_TOPOLOGY_PROFILE:-local-demo}"
BUILD_CACHE="${BUILD_CACHE:-$ROOT_DIR/build-neutron2-local-demo}"
SKIP_BUILD="false"

usage() {
  cat <<'EOF'
Usage: run_neutron2_local_demo.sh [options]

Runs the Neutron 2 laptop-only MVP demo:
  local F' app <-> PTY link emulator <-> fprime-gds
  PayloadAdapter_NeutronSim -> /tmp/neutron_payload_captures/*.csv
  Neutron 2 payload viewer -> http://127.0.0.1:8062

Options:
  --gui-port <port>          fprime-gds GUI port (default: 5050)
  --viewer-port <port>       payload viewer port (default: 8062)
  --delay <seconds>          scheduled collection delay (default: 10)
  --capture-seconds <secs>   simulator capture duration (default: 10)
  --dictionary <path>        topology dictionary path (default: latest generated dict)
  --build-cache <path>       local-demo build cache (default: ArtemisRpiTeensy_N2/build-neutron2-local-demo)
  --skip-build               use existing binary/dictionary without regenerating the local-demo profile
  --exit-after-sequence      stop emulator/viewer after automated checks pass
  -h, --help                 show this help text

Pass criteria:
  - GDS command path accepts the demo commands
  - scheduled collection produces a new neutron_capture_*.csv
  - F Prime payload downlink completes
  - payload viewer summary parses the generated CSV
  - payload viewer is opened/refocused after verified downlink
EOF
}

log() {
  printf '[neutron2-local-demo] %s\n' "$*"
}

fail() {
  printf '[neutron2-local-demo] ERROR: %s\n' "$*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --gui-port)
      GUI_PORT="${2:-}"
      shift 2
      ;;
    --viewer-port)
      VIEWER_PORT="${2:-}"
      shift 2
      ;;
    --delay)
      DELAY_SECONDS="${2:-}"
      shift 2
      ;;
    --capture-seconds)
      CAPTURE_SECONDS="${2:-}"
      shift 2
      ;;
    --dictionary)
      DICT_PATH="${2:-}"
      shift 2
      ;;
    --build-cache)
      BUILD_CACHE="${2:-}"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD="true"
      shift
      ;;
    --exit-after-sequence)
      HOLD_AFTER_SEQUENCE="false"
      shift
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
[[ -x "$ROOT_DIR/tools/run_local_emulation.sh" ]] || fail "Missing local emulator launcher"
[[ -f "$REPO_ROOT/ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py" ]] || fail "Missing payload viewer"
[[ -f "$REPO_ROOT/external/payload-neutron-simulation/neutron_payload_sim.py" ]] || fail "Missing neutron simulator"

# shellcheck disable=SC1090
. "$VENV_ACTIVATE"

if [[ "$SKIP_BUILD" != "true" ]]; then
  log "building topology profile: $TOPOLOGY_PROFILE"
  (
    cd "$ROOT_DIR"
    fprime-util generate -f --build-cache "$BUILD_CACHE" "-DNEUTRON2_TOPOLOGY_PROFILE=$TOPOLOGY_PROFILE"
    fprime-util build --build-cache "$BUILD_CACHE"
  )
fi

if [[ -z "$DICT_PATH" ]]; then
  DICT_PATH="$(
    python3 - "$ROOT_DIR/build-artifacts" "$DICT_BASENAME" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
name = sys.argv[2]
matches = list(root.glob(f"*/ArtemisRpiTeensyDeployment/dict/{name}"))
if not matches:
    raise SystemExit(1)
matches.sort(key=lambda path: path.stat().st_mtime, reverse=True)
print(matches[0])
PY
  )" || fail "No topology dictionary found. Build first: fprime-util generate -f && fprime-util build"
fi
[[ -f "$DICT_PATH" ]] || fail "Missing dictionary: $DICT_PATH"

mkdir -p "$ROOT_DIR/tools/logs" "$CAPTURE_DIR"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
LOG_DIR="$ROOT_DIR/tools/logs/neutron2_local_demo_$RUN_ID"
mkdir -p "$LOG_DIR"

EMU_PID=""
VIEWER_PID=""

cleanup() {
  local code=$?
  if [[ -n "$EMU_PID" ]] && kill -0 "$EMU_PID" >/dev/null 2>&1; then
    kill "$EMU_PID" >/dev/null 2>&1 || true
    wait "$EMU_PID" >/dev/null 2>&1 || true
  fi
  if [[ -n "$VIEWER_PID" ]] && kill -0 "$VIEWER_PID" >/dev/null 2>&1; then
    kill "$VIEWER_PID" >/dev/null 2>&1 || true
    wait "$VIEWER_PID" >/dev/null 2>&1 || true
  fi
  exit "$code"
}
trap cleanup EXIT INT TERM

wait_for_port() {
  local port="$1"
  local label="$2"
  python3 - "$port" "$label" <<'PY'
import socket
import sys
import time

port = int(sys.argv[1])
label = sys.argv[2]
deadline = time.monotonic() + 45
while time.monotonic() < deadline:
    sock = socket.socket()
    sock.settimeout(0.5)
    try:
        sock.connect(("127.0.0.1", port))
    except OSError:
        time.sleep(0.5)
    else:
        sock.close()
        raise SystemExit(0)
    finally:
        try:
            sock.close()
        except OSError:
            pass
print(f"Timed out waiting for {label} on 127.0.0.1:{port}", file=sys.stderr)
raise SystemExit(1)
PY
}

wait_for_log_pattern() {
  local pattern="$1"
  local label="$2"
  local deadline
  deadline=$((SECONDS + 20))
  while (( SECONDS < deadline )); do
    if grep -q "$pattern" "$LOG_DIR/emulation.log"; then
      return 0
    fi
    if grep -Eq "CaptureFailed|PayloadDownlinkFailed|DownlinkFailed" "$LOG_DIR/emulation.log"; then
      return 1
    fi
    sleep 0.5
  done
  printf '[neutron2-local-demo] timed out waiting for %s\n' "$label" >&2
  return 1
}

open_url() {
  local url="$1"
  if command -v open >/dev/null 2>&1; then
    open "$url" >/dev/null 2>&1 || true
  elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$url" >/dev/null 2>&1 || true
  fi
}

latest_capture_after() {
  local epoch="$1"
  python3 - "$CAPTURE_DIR" "$epoch" <<'PY'
from pathlib import Path
import sys

capture_dir = Path(sys.argv[1])
epoch = float(sys.argv[2])
matches = [
    path for path in capture_dir.glob("neutron_capture_*.csv")
    if path.stat().st_mtime >= epoch
]
if not matches:
    raise SystemExit(1)
matches.sort(key=lambda path: path.stat().st_mtime, reverse=True)
print(matches[0])
PY
}

verify_latest_payload() {
  local capture_file="$1"
  python3 - "$capture_file" "$CAPTURE_DIR/latest_payload.bin" <<'PY'
from pathlib import Path
import sys

capture = Path(sys.argv[1]).resolve()
latest = Path(sys.argv[2])
if not latest.exists():
    print(f"latest payload link does not exist: {latest}", file=sys.stderr)
    raise SystemExit(1)
if latest.resolve() != capture:
    print(f"latest payload points to {latest.resolve()}, expected {capture}", file=sys.stderr)
    raise SystemExit(1)
if latest.stat().st_size <= 0:
    print(f"latest payload is empty: {latest}", file=sys.stderr)
    raise SystemExit(1)
PY
}

send_command() {
  local command="$1"
  shift || true
  local cmd=(
    fprime-cli command-send
    "${DEPLOYMENT_NAME}.${command}"
    --dictionary "$DICT_PATH"
    --log-level-gds ERROR
  )
  if [[ $# -gt 0 ]]; then
    cmd+=(--arguments "$@")
  fi

  for attempt in 1 2 3 4 5; do
    if "${cmd[@]}"; then
      return 0
    fi
    log "command retry $attempt/5: $command"
    sleep 2
  done
  return 1
}

log "dictionary: $DICT_PATH"
log "capture dir: $CAPTURE_DIR"
log "logs: $LOG_DIR"

(
  cd "$ROOT_DIR"
  export NEUTRON_PAYLOAD_SIM_ROOT="$REPO_ROOT/external/payload-neutron-simulation"
  exec ./tools/run_local_emulation.sh --gui-port "$GUI_PORT" --link-mode channelized
) >"$LOG_DIR/emulation.log" 2>&1 &
EMU_PID="$!"
log "started local emulator pid=$EMU_PID; GDS: http://127.0.0.1:$GUI_PORT"

(
  cd "$REPO_ROOT"
  exec python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
    --capture-dir "$CAPTURE_DIR" \
    --port "$VIEWER_PORT" \
    --no-open
) >"$LOG_DIR/payload_viewer.log" 2>&1 &
VIEWER_PID="$!"
log "started payload viewer pid=$VIEWER_PID; viewer: http://127.0.0.1:$VIEWER_PORT"

wait_for_port "$GUI_PORT" "fprime-gds"
wait_for_port "$VIEWER_PORT" "payload viewer"

START_EPOCH="$(python3 -c 'import time; print(time.time())')"

log "sending demo command sequence"
send_command "missionManager.ENTER_BASE_MODE" || fail "Command failed: missionManager.ENTER_BASE_MODE"
send_command "sohManager.EMIT_SOH_SNAPSHOT" || fail "Command failed: sohManager.EMIT_SOH_SNAPSHOT"
send_command "scienceManager.CONFIGURE_CAPTURE_DURATION" "$CAPTURE_SECONDS" || fail "Command failed: scienceManager.CONFIGURE_CAPTURE_DURATION"
send_command "missionManager.SCHEDULE_COLLECTION" "$DELAY_SECONDS" || fail "Command failed: missionManager.SCHEDULE_COLLECTION"

WAIT_SECONDS=$((DELAY_SECONDS + 4))
log "waiting ${WAIT_SECONDS}s for scheduled capture"
sleep "$WAIT_SECONDS"

send_command "storageService.REPORT_LATEST_DATASET" || fail "Command failed: storageService.REPORT_LATEST_DATASET"
send_command "storageService.REPORT_STORAGE_HISTORY" || fail "Command failed: storageService.REPORT_STORAGE_HISTORY"
send_command "commsManager.REQUEST_SCIENCE_DOWNLINK" || fail "Command failed: commsManager.REQUEST_SCIENCE_DOWNLINK"
wait_for_log_pattern "PayloadDownlinkComplete" "payload downlink completion" || fail "Payload downlink did not complete"
wait_for_log_pattern "DownlinkFinished" "comms downlink completion" || fail "Comms downlink did not complete"

CAPTURE_FILE="$(latest_capture_after "$START_EPOCH")" || fail "No new neutron_capture_*.csv found in $CAPTURE_DIR"
log "new capture: $CAPTURE_FILE"
verify_latest_payload "$CAPTURE_FILE" || fail "F Prime did not publish the latest capture for downlink"

SUMMARY="$(
  python3 "$REPO_ROOT/ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py" \
    --summary "$CAPTURE_FILE"
)"
printf '%s\n' "$SUMMARY" > "$LOG_DIR/latest_capture_summary.json"
log "viewer summary written: $LOG_DIR/latest_capture_summary.json"
open_url "http://127.0.0.1:$VIEWER_PORT"
log "opened/refocused payload viewer after verified downlink: http://127.0.0.1:$VIEWER_PORT"
log "PASS: local Neutron 2 MVP demo sequence produced and parsed a science CSV"

if [[ "$HOLD_AFTER_SEQUENCE" == "true" ]]; then
  log "GDS: http://127.0.0.1:$GUI_PORT"
  log "payload viewer: http://127.0.0.1:$VIEWER_PORT"
  log "press Ctrl-C to stop local emulation"
  while true; do
    sleep 3600
  done
fi
