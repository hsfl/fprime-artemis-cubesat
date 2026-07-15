#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FPRIME_ROOT="$ROOT_DIR/ArtemisRpiTeensy_N2"
VENV_ACTIVATE="$FPRIME_ROOT/fprime-venv/bin/activate"

RUN_DEMO="true"
RUN_BUILD="true"
RUN_UT="true"
DEMO_PROFILE="${DEMO_PROFILE:-c3m}"

usage() {
  cat <<'EOF'
Usage: tools/validate_local.sh [options]

Runs the standard laptop-only validation path for the Neutron 2 F Prime repo.

Default checks:
  - shared-by-copy Teensy firmware modules have not drifted
  - generated transport headers match config/transport_constants.json
  - F Prime/Teensy transport constants agree
  - Python local-emulation and payload-receiver unit tests pass
  - unified topology generates and builds
  - F Prime component unit tests pass
  - automated local EPSCoR C3M demo completes three capture/downlink/decode cycles

Options:
  --skip-build     skip F Prime generate/build
  --skip-ut        skip F Prime component unit tests
  --skip-demo      skip automated local demo run
  --demo <profile> run local demo profile: c3m or neutron2 (default: c3m)
  -h, --help       show this help text
EOF
}

log() {
  printf '[validate-local] %s\n' "$*"
}

fail() {
  printf '[validate-local] ERROR: %s\n' "$*" >&2
  exit 1
}

check_shared_teensy_drift() {
  local shared_pairs=(
    "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/artemis_rf23bp.hpp|GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp"
    "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_counters.hpp|GDS_Teensy/firmware/gds_teensy/src/link_counters.hpp"
    "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/rf_tx_retry.hpp|GDS_Teensy/firmware/gds_teensy/src/rf_tx_retry.hpp"
    "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/wdt_guard.hpp|GDS_Teensy/firmware/gds_teensy/src/wdt_guard.hpp"
  )

  # rf23_driver.* and relay_uart_rf.cpp are shared in concept but intentionally diverge.
  # link_protocol.hpp also intentionally diverges: the satellite defines channel 2
  # (local Teensy RPC for PDU/RF status) that the ground Teensy does not carry.
  local pair satellite_path ground_path
  for pair in "${shared_pairs[@]}"; do
    IFS='|' read -r satellite_path ground_path <<< "$pair"
    cmp -s "$satellite_path" "$ground_path" || fail \
      "Firmware drift: $satellite_path != $ground_path; shared-by-copy modules must be byte-identical; fix BOTH sides"
  done
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build)
      RUN_BUILD="false"
      shift
      ;;
    --skip-ut)
      RUN_UT="false"
      shift
      ;;
    --skip-demo)
      RUN_DEMO="false"
      shift
      ;;
    --demo)
      DEMO_PROFILE="${2:-}"
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

[[ -f "$VENV_ACTIVATE" ]] || fail "Missing F Prime venv: $VENV_ACTIVATE"

cd "$ROOT_DIR"

log "checking shared Teensy firmware drift"
check_shared_teensy_drift

log "checking generated transport headers"
python3 tools/generate_transport_constants.py --check
python3 tools/check_transport_constants.py

log "running Python local-emulation tests"
python3 -m unittest \
  ArtemisRpiTeensy_N2/tools/tests/test_local_emulation_loop.py \
  ArtemisRpiTeensy_N2/tools/tests/test_c3m_local_demo_script.py \
  ArtemisRpiTeensy_N2/tools/tests/test_payload_receiver.py \
  ArtemisRpiTeensy_N2/tools/tests/test_c3m_payload_receiver_ui.py \
  ArtemisRpiTeensy_N2/tools/tests/test_rf_network_identity.py \
  GDS_Teensy/tools/tests/test_rf_tx_retry.py \
  GDS_Teensy/tools/tests/test_rf_msg_id_sequence.py \
  GDS_Teensy/tools/tests/test_usb_tx_progress.py

# shellcheck disable=SC1090
. "$VENV_ACTIVATE"

if [[ "$RUN_BUILD" == "true" ]]; then
  log "building unified topology"
  (
    cd "$FPRIME_ROOT"
    fprime-util generate -f
    fprime-util build
  )
fi

if [[ "$RUN_UT" == "true" ]]; then
  log "running F Prime component unit tests"
  (
    cd "$FPRIME_ROOT"
    fprime-util generate --ut -f
    fprime-util build --ut
    fprime-util check --all
  )
fi

if [[ "$RUN_DEMO" == "true" ]]; then
  case "$DEMO_PROFILE" in
    c3m)
      log "running automated local EPSCoR C3M demo sequence"
      (
        cd "$FPRIME_ROOT"
        ./tools/run_c3m_local_demo.sh \
          --skip-build \
          --exit-after-sequence \
          --no-open \
          --gui-port 5061 \
          --delay 2 \
          --captures 3
      )
      ;;
    neutron2)
      log "running automated local Neutron 2 demo sequence"
      (
        cd "$FPRIME_ROOT"
        ./tools/run_neutron2_local_demo.sh \
          --skip-build \
          --exit-after-sequence \
          --gui-port 5061 \
          --viewer-port 8063 \
          --no-open \
          --delay 2 \
          --capture-seconds 2
      )
      ;;
    *)
      fail "Unknown demo profile: $DEMO_PROFILE"
      ;;
  esac
fi

log "PASS"
