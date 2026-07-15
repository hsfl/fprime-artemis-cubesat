#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
VENV_ACTIVATE="$ROOT_DIR/fprime-venv/bin/activate"
DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
DICT_BASENAME="${DEPLOYMENT_NAME}TopologyDictionary.json"
GUI_PORT="${GUI_PORT:-5050}"
DELAY_SECONDS="${DELAY_SECONDS:-10}"
CAPTURES="${CAPTURES:-3}"
HOLD_AFTER_SEQUENCE="true"
DICT_PATH="${DICT_PATH:-}"
APP_BINARY_PATH="${APP_BINARY_PATH:-}"
BUILD_CACHE="${BUILD_CACHE:-$ROOT_DIR/build-c3m-local}"
SKIP_BUILD="false"
GENERATE_PNG="true"
OPEN_PNG="true"
LEPTON_SAMPLE_CSV="${C3M_LEPTON_SAMPLE_CSV:-$REPO_ROOT/ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv}"
LEPTON_CAMERA_BACKEND="${LEPTON_CAMERA_BACKEND:-sample}"
PAYLOAD_RECEIVER_TIMEOUT_SECONDS="${PAYLOAD_RECEIVER_TIMEOUT_SECONDS:-180}"
DROP_PAYLOAD_DATA_INDEX=""
RESTART_RECEIVER_CYCLE=""
ABANDON_FIRST_CYCLE="false"
PARTIAL_TRANSFER_TIMEOUT_SECONDS="${PARTIAL_TRANSFER_TIMEOUT_SECONDS:-2}"

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
  --captures <count>         consecutive capture/downlink/view cycles (default: 3)
  --drop-payload-data-index <index>
                             drop one N2 DATA packet once; its repair must pass
  --restart-receiver-cycle <cycle>
                             restart/checkpoint-resume receiver during this cycle
  --abandon-first-cycle     permanently lose DATA 100 in cycle 1, save an
                             honest partial, then require later cycles to pass
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
  - every scheduled collection writes a distinct ./DpCat/Dp_*.fdp
  - every F Prime payload downlink completes over emulated channel 1
  - the real ground payload receiver reconstructs one distinct .fdp per cycle
  - the Lepton viewer decodes each ground-received .fdp and verifies a 160x120 thermal frame
  - interactive runs open the final decoded PNG image
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
    --captures)
      CAPTURES="${2:-}"
      shift 2
      ;;
    --drop-payload-data-index)
      DROP_PAYLOAD_DATA_INDEX="${2:-}"
      shift 2
      ;;
    --restart-receiver-cycle)
      RESTART_RECEIVER_CYCLE="${2:-}"
      shift 2
      ;;
    --abandon-first-cycle)
      ABANDON_FIRST_CYCLE="true"
      shift
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

[[ "$CAPTURES" =~ ^[0-9]+$ ]] && (( CAPTURES > 0 )) || fail "--captures must be a positive integer"
[[ "$DELAY_SECONDS" =~ ^[0-9]+$ ]] && (( DELAY_SECONDS >= 1 && DELAY_SECONDS <= 300 )) || \
  fail "--delay must be an integer from 1 through 300"
if [[ -n "$DROP_PAYLOAD_DATA_INDEX" ]]; then
  [[ "$DROP_PAYLOAD_DATA_INDEX" =~ ^[0-9]+$ ]] && (( DROP_PAYLOAD_DATA_INDEX <= 65535 )) || \
    fail "--drop-payload-data-index must be an integer from 0 through 65535"
fi
if [[ -n "$RESTART_RECEIVER_CYCLE" ]]; then
  [[ "$RESTART_RECEIVER_CYCLE" =~ ^[0-9]+$ ]] && \
    (( RESTART_RECEIVER_CYCLE >= 1 && RESTART_RECEIVER_CYCLE <= CAPTURES )) || \
    fail "--restart-receiver-cycle must identify one requested capture cycle"
fi
if [[ "$ABANDON_FIRST_CYCLE" == "true" ]]; then
  (( CAPTURES >= 2 )) || fail "--abandon-first-cycle requires --captures 2 or greater"
  [[ -z "$DROP_PAYLOAD_DATA_INDEX" && -z "$RESTART_RECEIVER_CYCLE" ]] || \
    fail "--abandon-first-cycle cannot be combined with another receiver fault"
fi

[[ -f "$VENV_ACTIVATE" ]] || fail "Missing venv: $VENV_ACTIVATE"
[[ -x "$ROOT_DIR/tools/run_local_emulation.sh" ]] || fail "Missing local emulator launcher"
[[ -x "$REPO_ROOT/ground-station/lepton-dp-viewer/lepton_dp_viewer.py" ]] || fail "Missing Lepton DP viewer"
[[ -f "$LEPTON_SAMPLE_CSV" ]] || fail "Missing real Lepton sample CSV: $LEPTON_SAMPLE_CSV"

# shellcheck disable=SC1090
. "$VENV_ACTIVATE"
export C3M_LEPTON_SAMPLE_CSV="$LEPTON_SAMPLE_CSV"
export LEPTON_CAMERA_BACKEND

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
GROUND_RECEIVED_DIR="$LOG_DIR/ground_received"
RECEIVER_CHECKPOINT_DIR="$LOG_DIR/receiver_checkpoint"
mkdir -p "$LOG_DIR" "$DECODE_DIR" "$GROUND_RECEIVED_DIR"

EMU_PID=""
RECEIVER_PID=""

cleanup() {
  local code=$?
  if [[ -n "$RECEIVER_PID" ]] && kill -0 "$RECEIVER_PID" >/dev/null 2>&1; then
    kill "$RECEIVER_PID" >/dev/null 2>&1 || true
    wait "$RECEIVER_PID" >/dev/null 2>&1 || true
  fi
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

wait_for_log_count() {
  local pattern="$1"
  local expected_count="$2"
  local label="$3"
  local deadline
  deadline=$((SECONDS + 150))
  while (( SECONDS < deadline )); do
    local actual_count
    actual_count="$(grep -c "$pattern" "$LOG_DIR/emulation.log" 2>/dev/null || true)"
    if (( actual_count >= expected_count )); then
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

wait_for_payload_uart() {
  local deadline
  deadline=$((SECONDS + 45))
  while (( SECONDS < deadline )); do
    local payload_uart
    payload_uart="$(sed -n 's/^PAYLOAD_UART_DEVICE=//p' "$LOG_DIR/emulation.log" 2>/dev/null | tail -n 1)"
    if [[ -n "$payload_uart" && -e "$payload_uart" ]]; then
      printf '%s\n' "$payload_uart"
      return 0
    fi
    if ! kill -0 "$EMU_PID" >/dev/null 2>&1; then
      return 1
    fi
    sleep 0.25
  done
  return 1
}

wait_for_receiver_ready() {
  local expected_count="$1"
  local deadline
  deadline=$((SECONDS + 15))
  while (( SECONDS < deadline )); do
    local actual_count
    actual_count="$(grep -c '^listening on ' "$LOG_DIR/payload_receiver.log" 2>/dev/null || true)"
    if (( actual_count >= expected_count )); then
      return 0
    fi
    if ! kill -0 "$RECEIVER_PID" >/dev/null 2>&1; then
      return 1
    fi
    sleep 0.25
  done
  return 1
}

wait_for_receiver_progress_after() {
  local baseline="$1"
  local deadline
  deadline=$((SECONDS + 45))
  while (( SECONDS < deadline )); do
    local actual_count
    actual_count="$(grep -c '^progress:' "$LOG_DIR/payload_receiver.log" 2>/dev/null || true)"
    if (( actual_count > baseline )); then
      return 0
    fi
    if ! kill -0 "$RECEIVER_PID" >/dev/null 2>&1; then
      return 1
    fi
    sleep 0.25
  done
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

count_ground_products() {
  find "$GROUND_RECEIVED_DIR" -maxdepth 1 -type f -name '*.fdp' | wc -l | tr -d ' '
}

count_partial_products() {
  find "$GROUND_RECEIVED_DIR" -maxdepth 1 -type f -name '*.fdp.partial' | wc -l | tr -d ' '
}

ground_product_at() {
  local index="$1"
  python3 - "$GROUND_RECEIVED_DIR" "$index" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
index = int(sys.argv[2])
products = sorted(root.glob("*.fdp"), key=lambda path: (path.stat().st_mtime_ns, path.name))
if len(products) <= index:
    raise SystemExit(1)
print(products[index])
PY
}

partial_product_at() {
  local index="$1"
  python3 - "$GROUND_RECEIVED_DIR" "$index" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
index = int(sys.argv[2])
products = sorted(root.glob("*.fdp.partial"), key=lambda path: (path.stat().st_mtime_ns, path.name))
if len(products) <= index:
    raise SystemExit(1)
print(products[index])
PY
}

wait_for_ground_product_count() {
  local expected_count="$1"
  local deadline
  deadline=$((SECONDS + PAYLOAD_RECEIVER_TIMEOUT_SECONDS))
  while (( SECONDS < deadline )); do
    if [[ -n "$RECEIVER_PID" ]] && ! kill -0 "$RECEIVER_PID" >/dev/null 2>&1; then
      return 1
    fi
    local actual_count
    actual_count="$(count_ground_products)"
    if (( actual_count >= expected_count )); then
      return 0
    fi
    sleep 0.5
  done
  return 1
}

wait_for_partial_product_count() {
  local expected_count="$1"
  local deadline
  deadline=$((SECONDS + 30))
  while (( SECONDS < deadline )); do
    if [[ -n "$RECEIVER_PID" ]] && ! kill -0 "$RECEIVER_PID" >/dev/null 2>&1; then
      return 1
    fi
    local actual_count
    actual_count="$(count_partial_products)"
    if (( actual_count >= expected_count )); then
      return 0
    fi
    sleep 0.25
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

start_payload_receiver() {
  local ready_baseline
  ready_baseline="$(grep -c '^listening on ' "$LOG_DIR/payload_receiver.log" 2>/dev/null || true)"
  local receiver_args=(
    --port "$PAYLOAD_UART_DEVICE"
    --output-dir "$GROUND_RECEIVED_DIR"
    --ext .fdp
    --timeout "$PAYLOAD_RECEIVER_TIMEOUT_SECONDS"
  )
  if [[ -n "$RESTART_RECEIVER_CYCLE" ]]; then
    receiver_args+=(--checkpoint-dir "$RECEIVER_CHECKPOINT_DIR")
  fi
  if [[ "$ABANDON_FIRST_CYCLE" == "true" ]]; then
    receiver_args+=(
      --transfer-timeout "$PARTIAL_TRANSFER_TIMEOUT_SECONDS"
      --absolute-transfer-timeout 150
      --save-partial-on-timeout
    )
  fi
  PYTHONUNBUFFERED=1 python3 "$ROOT_DIR/tools/payload_receiver.py" \
    "${receiver_args[@]}" >>"$LOG_DIR/payload_receiver.log" 2>&1 &
  RECEIVER_PID="$!"
  log "started ground payload receiver pid=$RECEIVER_PID"
  wait_for_receiver_ready "$((ready_baseline + 1))" || \
    fail "Ground payload receiver did not become ready; see $LOG_DIR/payload_receiver.log"
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

decode_ground_product() {
  local cycle="$1"
  local fdp_file="$2"
  local cycle_decode_dir="$DECODE_DIR/cycle_$cycle"
  local summary_file="$LOG_DIR/lepton_summary_cycle_$cycle.json"
  local viewer_args=(--dictionary "$DICT_PATH" --outdir "$cycle_decode_dir" --summary --no-show)
  mkdir -p "$cycle_decode_dir"
  if [[ "$GENERATE_PNG" != "true" ]]; then
    viewer_args+=(--no-png)
  fi

  python3 "$REPO_ROOT/ground-station/lepton-dp-viewer/lepton_dp_viewer.py" "$fdp_file" "${viewer_args[@]}" \
    > "$summary_file"

  LAST_PNG="$(python3 - "$summary_file" "$GENERATE_PNG" "$LEPTON_SAMPLE_CSV" <<'PY'
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

  log "cycle $cycle viewer summary: $summary_file"
  log "cycle $cycle decoded ground Lepton CSV matches real sample data"
  if [[ -n "$LAST_PNG" ]]; then
    log "cycle $cycle viewer PNG: $LAST_PNG"
  fi
}

log "app binary: $APP_BINARY_PATH"
log "dictionary: $DICT_PATH"
log "data products: $ROOT_DIR/DpCat"
log "real Lepton sample CSV: $LEPTON_SAMPLE_CSV"
log "Lepton camera backend: $LEPTON_CAMERA_BACKEND"
log "capture/downlink cycles: $CAPTURES"
if [[ -n "$DROP_PAYLOAD_DATA_INDEX" ]]; then
  log "one-shot payload DATA drop index: $DROP_PAYLOAD_DATA_INDEX"
fi
if [[ -n "$RESTART_RECEIVER_CYCLE" ]]; then
  log "receiver checkpoint/restart cycle: $RESTART_RECEIVER_CYCLE"
fi
if [[ "$ABANDON_FIRST_CYCLE" == "true" ]]; then
  log "cycle 1 expected result: honest partial after permanent DATA loss"
fi
log "logs: $LOG_DIR"

EMULATOR_ARGS=(
  --app-binary "$APP_BINARY_PATH"
  --dictionary "$DICT_PATH"
  --gui-port "$GUI_PORT"
  --link-mode channelized
)
if [[ -n "$DROP_PAYLOAD_DATA_INDEX" ]]; then
  EMULATOR_ARGS+=(--drop-payload-data-index "$DROP_PAYLOAD_DATA_INDEX")
fi
if [[ "$ABANDON_FIRST_CYCLE" == "true" ]]; then
  EMULATOR_ARGS+=(--blackhole-payload-data-index-first-transfer 100)
fi

(
  cd "$ROOT_DIR"
  exec ./tools/run_local_emulation.sh "${EMULATOR_ARGS[@]}"
) >"$LOG_DIR/emulation.log" 2>&1 &
EMU_PID="$!"
log "started local emulator pid=$EMU_PID; GDS: http://127.0.0.1:$GUI_PORT"

PAYLOAD_UART_DEVICE="$(wait_for_payload_uart)" || fail "Local emulator did not publish a payload UART device"
log "payload UART: $PAYLOAD_UART_DEVICE"

start_payload_receiver

wait_for_port "$GUI_PORT" "fprime-gds"

log "sending one-time C3M startup commands"
send_command "missionApp.ENTER_BASE_MODE" || fail "Command failed: missionApp.ENTER_BASE_MODE"
send_command "sohApp.EMIT_SOH_SNAPSHOT" || fail "Command failed: sohApp.EMIT_SOH_SNAPSHOT"

SOURCE_PRODUCT_COUNT="$(find "$ROOT_DIR/DpCat" -maxdepth 1 -type f -name 'Dp_*.fdp' | wc -l | tr -d ' ')"
GROUND_PRODUCT_BASELINE="$(count_ground_products)"
PARTIAL_PRODUCT_BASELINE="$(count_partial_products)"
COMPLETED_GROUND_PRODUCTS=0
HISTORY_FILE="$LOG_DIR/cycle_history.tsv"
printf 'cycle\tresult\tsatellite_product\tground_product\tdecode_summary\n' >"$HISTORY_FILE"
LAST_PNG=""

for ((cycle = 1; cycle <= CAPTURES; cycle++)); do
  START_EPOCH="$(python3 -c 'import time; print(time.time())')"
  log "cycle $cycle/$CAPTURES: scheduling Lepton capture after ${DELAY_SECONDS}s"
  send_command "missionApp.SCHEDULE_COLLECTION" "$DELAY_SECONDS" || \
    fail "Cycle $cycle command failed: missionApp.SCHEDULE_COLLECTION"

  wait_for_log_count "ScienceStored" "$cycle" "cycle $cycle ScienceStored" || \
    fail "Cycle $cycle did not reach ScienceStored"
  FDP_FILE="$(wait_for_fdp_after "$START_EPOCH")" || \
    fail "Cycle $cycle produced no new Lepton Dp_*.fdp in $ROOT_DIR/DpCat"
  CURRENT_SOURCE_COUNT="$(find "$ROOT_DIR/DpCat" -maxdepth 1 -type f -name 'Dp_*.fdp' | wc -l | tr -d ' ')"
  (( CURRENT_SOURCE_COUNT > SOURCE_PRODUCT_COUNT )) || \
    fail "Cycle $cycle did not create a distinct satellite-side data product"
  SOURCE_PRODUCT_COUNT="$CURRENT_SOURCE_COUNT"
  log "cycle $cycle satellite product: $FDP_FILE"

  RECEIVER_PROGRESS_BASELINE="$(grep -c '^progress:' "$LOG_DIR/payload_receiver.log" 2>/dev/null || true)"
  send_command "commsApp.REQUEST_SCIENCE_DOWNLINK" || \
    fail "Cycle $cycle command failed: commsApp.REQUEST_SCIENCE_DOWNLINK"
  if [[ "$RESTART_RECEIVER_CYCLE" == "$cycle" ]]; then
    wait_for_receiver_progress_after "$RECEIVER_PROGRESS_BASELINE" || \
      fail "Cycle $cycle receiver never made progress before restart"
    log "cycle $cycle: restarting ground receiver after checkpointed progress"
    kill "$RECEIVER_PID" >/dev/null 2>&1 || true
    wait "$RECEIVER_PID" >/dev/null 2>&1 || true
    RECEIVER_PID=""
    start_payload_receiver
  fi
  wait_for_log_count "PayloadDownlinkComplete" "$cycle" "cycle $cycle payload completion" || \
    fail "Cycle $cycle payload downlink did not complete"
  wait_for_log_count "DownlinkFinished" "$cycle" "cycle $cycle comms completion" || \
    fail "Cycle $cycle comms downlink did not complete"

  if [[ "$ABANDON_FIRST_CYCLE" == "true" && "$cycle" == "1" ]]; then
    EXPECTED_PARTIAL_COUNT=$((PARTIAL_PRODUCT_BASELINE + 1))
    wait_for_partial_product_count "$EXPECTED_PARTIAL_COUNT" || \
      fail "Cycle 1 did not terminate as an honest partial"
    PARTIAL_FDP_FILE="$(partial_product_at "$((EXPECTED_PARTIAL_COUNT - 1))")" || \
      fail "Cycle 1 partial artifact could not be selected"
    [[ "$(count_ground_products)" == "$GROUND_PRODUCT_BASELINE" ]] || \
      fail "Cycle 1 incorrectly produced a complete ground artifact"
    log "cycle 1 expected partial product: $PARTIAL_FDP_FILE"
    printf '%s\t%s\t%s\t%s\t%s\n' \
      "$cycle" "PARTIAL_EXPECTED" "$FDP_FILE" "$PARTIAL_FDP_FILE" "" \
      >>"$HISTORY_FILE"
    continue
  fi

  COMPLETED_GROUND_PRODUCTS=$((COMPLETED_GROUND_PRODUCTS + 1))
  EXPECTED_GROUND_COUNT=$((GROUND_PRODUCT_BASELINE + COMPLETED_GROUND_PRODUCTS))
  wait_for_ground_product_count "$EXPECTED_GROUND_COUNT" || \
    fail "Cycle $cycle produced no CRC-valid ground-received artifact; see $LOG_DIR/payload_receiver.log"
  GROUND_FDP_FILE="$(ground_product_at "$((EXPECTED_GROUND_COUNT - 1))")" || \
    fail "Cycle $cycle ground artifact could not be selected"
  log "cycle $cycle ground product: $GROUND_FDP_FILE"
  decode_ground_product "$cycle" "$GROUND_FDP_FILE"
  printf '%s\t%s\t%s\t%s\t%s\n' \
    "$cycle" "COMPLETE" "$FDP_FILE" "$GROUND_FDP_FILE" "$LOG_DIR/lepton_summary_cycle_$cycle.json" \
    >>"$HISTORY_FILE"
done

if [[ -n "$LAST_PNG" && "$OPEN_PNG" == "true" ]]; then
  log "opening final decoded Lepton PNG"
  open_png_viewer "$LAST_PNG"
fi
if [[ "$ABANDON_FIRST_CYCLE" == "true" ]]; then
  log "PASS: cycle 1 failed honestly and $COMPLETED_GROUND_PRODUCTS later capture(s) completed cleanly"
else
  log "PASS: completed $CAPTURES consecutive C3M capture/downlink/decode cycles from ground-received artifacts"
fi
log "cycle history: $HISTORY_FILE"

if [[ "$HOLD_AFTER_SEQUENCE" == "true" ]]; then
  log "GDS: http://127.0.0.1:$GUI_PORT"
  log "press Ctrl-C to stop local emulation"
  while true; do
    sleep 3600
  done
fi
