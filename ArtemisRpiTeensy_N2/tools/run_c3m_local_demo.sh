#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
VENV_ACTIVATE="$ROOT_DIR/fprime-venv/bin/activate"
DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
DICT_BASENAME="${DEPLOYMENT_NAME}TopologyDictionary.json"
GUI_PORT="${GUI_PORT:-5050}"
DELAY_SECONDS="${DELAY_SECONDS:-10}"
CAPTURE_SECONDS="${CAPTURE_SECONDS:-10}"
HOLD_AFTER_SEQUENCE="true"
DICT_PATH="${DICT_PATH:-}"
APP_BINARY_PATH="${APP_BINARY_PATH:-}"
BUILD_CACHE="${BUILD_CACHE:-$ROOT_DIR/build-c3m-local}"
SKIP_BUILD="false"
GENERATE_PNG="true"
OPEN_PNG="true"
LEPTON_SAMPLE_CSV="${C3M_LEPTON_SAMPLE_CSV:-$REPO_ROOT/TEST-DATA-DOWNLINK/data/Dp_20260707_120740.csv}"

usage() {
  cat <<'EOF'
Usage: run_c3m_local_demo.sh [options]

Runs the EPSCoR C3M laptop-only demo:
  local F' app <-> PTY link emulator <-> fprime-gds
  PayloadDriver_Lepton -> ArtemisDataProducts ./DpCat/*.fdp
  Lepton data-product decoder -> JSON/CSV/PNG under tools/logs

Options:
  --gui-port <port>          fprime-gds GUI port (default: 5050)
  --delay <seconds>          scheduled collection delay (default: 10)
  --capture-seconds <secs>   accepted by the shared science flow; Lepton captures one frame (default: 10)
  --app-binary <path>        deployment binary path (default: host-platform artifact)
  --dictionary <path>        topology dictionary path (default: latest generated dict)
  --build-cache <path>       local build cache (default: ArtemisRpiTeensy_N2/build-c3m-local)
  --sample-csv <path>        real Lepton sample CSV to feed the local camera stub
  --skip-build               use existing binary/dictionary without regenerating the unified topology
  --exit-after-sequence      stop emulator after automated checks pass
  --png                      render PNG output (default; kept for compatibility)
  --no-png                   skip PNG generation
  --no-open, --no-show       do not open the decoded PNG viewer
  -h, --help                 show this help text

Pass criteria:
  - GDS command path accepts the demo commands
  - scheduled collection writes a new ./DpCat/Dp_*.fdp
  - F Prime payload downlink completes over channel 1
  - Lepton viewer decodes the .fdp and verifies a 160x120 thermal frame
  - interactive runs open the decoded PNG image
EOF
}

log() {
  printf '[c3m-demo] %s\n' "$*"
}

fail() {
  printf '[c3m-demo] ERROR: %s\n' "$*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --gui-port)
      GUI_PORT="${2:-}"
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
    --app-binary)
      APP_BINARY_PATH="${2:-}"
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
    --sample-csv)
      LEPTON_SAMPLE_CSV="${2:-}"
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
    --png)
      GENERATE_PNG="true"
      shift
      ;;
    --no-png)
      GENERATE_PNG="false"
      OPEN_PNG="false"
      shift
      ;;
    --no-open|--no-show)
      OPEN_PNG="false"
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
[[ -x "$REPO_ROOT/ground-station/lepton-dp-viewer/lepton_dp_viewer.py" ]] || fail "Missing Lepton DP viewer"
[[ -f "$LEPTON_SAMPLE_CSV" ]] || fail "Missing real Lepton sample CSV: $LEPTON_SAMPLE_CSV"

# shellcheck disable=SC1090
. "$VENV_ACTIVATE"
export C3M_LEPTON_SAMPLE_CSV="$LEPTON_SAMPLE_CSV"

if [[ "$SKIP_BUILD" != "true" ]]; then
  log "building unified C3M topology"
  (
    cd "$ROOT_DIR"
    fprime-util generate -f --build-cache "$BUILD_CACHE"
    fprime-util build --build-cache "$BUILD_CACHE"
  )
fi

resolve_artifact() {
  local kind="$1"
  local name="$2"
  python3 - "$ROOT_DIR/build-artifacts" "$DEPLOYMENT_NAME" "$kind" "$name" <<'PY'
from pathlib import Path
import platform
import sys

root = Path(sys.argv[1])
deployment = sys.argv[2]
kind = sys.argv[3]
name = sys.argv[4]

host = platform.system()
platform_dir = "Darwin" if host == "Darwin" else "Linux" if host == "Linux" else host
host_path = root / platform_dir / deployment / kind / name
if host_path.exists():
    print(host_path)
    raise SystemExit(0)

matches = list(root.glob(f"*/{deployment}/{kind}/{name}"))
if not matches:
    raise SystemExit(1)
matches.sort(key=lambda path: path.stat().st_mtime, reverse=True)
print(matches[0])
PY
}

if [[ -z "$APP_BINARY_PATH" ]]; then
  APP_BINARY_PATH="$(resolve_artifact bin "$DEPLOYMENT_NAME")" || fail "No deployment binary found. Build first."
fi
[[ -x "$APP_BINARY_PATH" ]] || fail "Missing or non-executable deployment binary: $APP_BINARY_PATH"

if [[ -z "$DICT_PATH" ]]; then
  DICT_PATH="$(resolve_artifact dict "$DICT_BASENAME")" || fail "No topology dictionary found. Build first."
fi
[[ -f "$DICT_PATH" ]] || fail "Missing dictionary: $DICT_PATH"

mkdir -p "$ROOT_DIR/tools/logs" "$ROOT_DIR/DpCat"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
LOG_DIR="$ROOT_DIR/tools/logs/c3m_local_demo_$RUN_ID"
DECODE_DIR="$LOG_DIR/lepton_decode"
mkdir -p "$LOG_DIR" "$DECODE_DIR"

EMU_PID=""

cleanup() {
  local code=$?
  if [[ -n "$EMU_PID" ]] && kill -0 "$EMU_PID" >/dev/null 2>&1; then
    kill "$EMU_PID" >/dev/null 2>&1 || true
    wait "$EMU_PID" >/dev/null 2>&1 || true
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
  deadline=$((SECONDS + 90))
  while (( SECONDS < deadline )); do
    if grep -q "$pattern" "$LOG_DIR/emulation.log"; then
      return 0
    fi
    if grep -Eq "ImageCaptureFailed|PayloadDownlinkFailed|DownlinkFailed" "$LOG_DIR/emulation.log"; then
      return 1
    fi
    sleep 0.5
  done
  printf '[c3m-demo] timed out waiting for %s\n' "$label" >&2
  return 1
}

latest_fdp_after() {
  local epoch="$1"
  python3 - "$ROOT_DIR/DpCat" "$epoch" <<'PY'
from pathlib import Path
import sys

dp_dir = Path(sys.argv[1])
epoch = float(sys.argv[2])
matches = [
    path for path in dp_dir.glob("Dp_*.fdp")
    if path.stat().st_mtime >= epoch and path.stat().st_size >= 38000
]
if not matches:
    raise SystemExit(1)
matches.sort(key=lambda path: path.stat().st_mtime, reverse=True)
print(matches[0])
PY
}

wait_for_fdp_after() {
  local epoch="$1"
  local deadline
  deadline=$((SECONDS + 35))
  while (( SECONDS < deadline )); do
    if latest_fdp_after "$epoch"; then
      return 0
    fi
    sleep 0.5
  done
  return 1
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

open_png_viewer() {
  local png_path="$1"
  [[ -f "$png_path" ]] || fail "PNG not found: $png_path"

  case "$(uname -s)" in
    Darwin)
      open "$png_path" >/dev/null 2>&1 || fail "Failed to open PNG viewer for $png_path"
      ;;
    Linux)
      if command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$png_path" >/dev/null 2>&1 || fail "Failed to open PNG viewer for $png_path"
      elif command -v wslview >/dev/null 2>&1; then
        wslview "$png_path" >/dev/null 2>&1 || fail "Failed to open PNG viewer for $png_path"
      else
        log "PNG written but no image opener was found: $png_path"
      fi
      ;;
    MINGW*|MSYS*|CYGWIN*)
      cmd.exe /C start "" "$png_path" >/dev/null 2>&1 || fail "Failed to open PNG viewer for $png_path"
      ;;
    *)
      log "PNG written: $png_path"
      ;;
  esac
}

log "app binary: $APP_BINARY_PATH"
log "dictionary: $DICT_PATH"
log "data products: $ROOT_DIR/DpCat"
log "real Lepton sample CSV: $LEPTON_SAMPLE_CSV"
log "logs: $LOG_DIR"

(
  cd "$ROOT_DIR"
  exec ./tools/run_local_emulation.sh \
    --app-binary "$APP_BINARY_PATH" \
    --dictionary "$DICT_PATH" \
    --gui-port "$GUI_PORT" \
    --link-mode channelized
) >"$LOG_DIR/emulation.log" 2>&1 &
EMU_PID="$!"
log "started local emulator pid=$EMU_PID; GDS: http://127.0.0.1:$GUI_PORT"

wait_for_port "$GUI_PORT" "fprime-gds"

START_EPOCH="$(python3 -c 'import time; print(time.time())')"

log "sending C3M command sequence"
send_command "missionApp.ENTER_BASE_MODE" || fail "Command failed: missionApp.ENTER_BASE_MODE"
send_command "sohApp.EMIT_SOH_SNAPSHOT" || fail "Command failed: sohApp.EMIT_SOH_SNAPSHOT"
send_command "payloadDriverLepton.ENABLE" || fail "Command failed: payloadDriverLepton.ENABLE"
send_command "scienceApp.CONFIGURE_CAPTURE_DURATION" "$CAPTURE_SECONDS" || fail "Command failed: scienceApp.CONFIGURE_CAPTURE_DURATION"
send_command "missionApp.SCHEDULE_COLLECTION" "$DELAY_SECONDS" || fail "Command failed: missionApp.SCHEDULE_COLLECTION"

WAIT_SECONDS=$((DELAY_SECONDS + 6))
log "waiting ${WAIT_SECONDS}s for scheduled Lepton capture"
sleep "$WAIT_SECONDS"

FDP_FILE="$(wait_for_fdp_after "$START_EPOCH")" || fail "No new Lepton Dp_*.fdp found in $ROOT_DIR/DpCat"
log "new Lepton data product: $FDP_FILE"

send_command "storageManager.REPORT_LATEST_DATASET" || fail "Command failed: storageManager.REPORT_LATEST_DATASET"
send_command "storageManager.REPORT_STORAGE_HISTORY" || fail "Command failed: storageManager.REPORT_STORAGE_HISTORY"
send_command "commsApp.REQUEST_SCIENCE_DOWNLINK" || fail "Command failed: commsApp.REQUEST_SCIENCE_DOWNLINK"
wait_for_log_pattern "PayloadDownlinkComplete" "payload downlink completion" || fail "Payload downlink did not complete"
wait_for_log_pattern "DownlinkFinished" "comms downlink completion" || fail "Comms downlink did not complete"

VIEWER_ARGS=(--dictionary "$DICT_PATH" --outdir "$DECODE_DIR" --summary --no-show)
if [[ "$GENERATE_PNG" != "true" ]]; then
  VIEWER_ARGS+=(--no-png)
fi
python3 "$REPO_ROOT/ground-station/lepton-dp-viewer/lepton_dp_viewer.py" "$FDP_FILE" "${VIEWER_ARGS[@]}" \
  > "$LOG_DIR/lepton_summary.json"

PNG_FILE="$(python3 - "$LOG_DIR/lepton_summary.json" "$GENERATE_PNG" "$LEPTON_SAMPLE_CSV" <<'PY'
import json
import sys

summary = json.load(open(sys.argv[1]))
expect_png = sys.argv[2] == "true"
expected_csv = sys.argv[3]
if summary.get("width") != 160 or summary.get("height") != 120 or summary.get("pixels") != 19200:
    raise SystemExit("decoded Lepton dimensions did not match 160x120")
if summary.get("max_c", 0) <= summary.get("min_c", 0):
    raise SystemExit("decoded Lepton thermal range is invalid")
png = summary.get("png")
if expect_png and not png:
    raise SystemExit("decoded Lepton PNG was not generated")
decoded_csv = summary.get("csv")
if not decoded_csv:
    raise SystemExit("decoded Lepton CSV was not generated")

def read_grid(path):
    rows = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            rows.append([float(value) for value in line.split(",")])
    if len(rows) != 120 or any(len(row) != 160 for row in rows):
        raise SystemExit(f"{path} is not a 120x160 Lepton CSV grid")
    return rows

actual = read_grid(decoded_csv)
expected = read_grid(expected_csv)
for row_idx, (actual_row, expected_row) in enumerate(zip(actual, expected)):
    for col_idx, (actual_value, expected_value) in enumerate(zip(actual_row, expected_row)):
        if abs(actual_value - expected_value) > 0.005:
            raise SystemExit(
                f"decoded Lepton sample mismatch at row={row_idx} col={col_idx}: "
                f"{actual_value:.2f} != {expected_value:.2f}"
            )
if png:
    print(png)
PY
)"

log "viewer summary written: $LOG_DIR/lepton_summary.json"
log "decoded Lepton CSV matches real sample data"
if [[ -n "$PNG_FILE" ]]; then
  log "viewer PNG written: $PNG_FILE"
  if [[ "$OPEN_PNG" == "true" ]]; then
    log "opening decoded Lepton PNG"
    open_png_viewer "$PNG_FILE"
  fi
fi
log "PASS: local EPSCoR C3M demo produced, downlinked, and decoded a Lepton .fdp"

if [[ "$HOLD_AFTER_SEQUENCE" == "true" ]]; then
  log "GDS: http://127.0.0.1:$GUI_PORT"
  log "press Ctrl-C to stop local emulation"
  while true; do
    sleep 3600
  done
fi
