#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FPRIME_ROOT="$ROOT_DIR/ArtemisRpiTeensy_N2"
VENV_ACTIVATE="$FPRIME_ROOT/fprime-venv/bin/activate"

RUN_DEMO="true"
RUN_BUILD="true"
RUN_UT="true"

usage() {
  cat <<'EOF'
Usage: tools/validate_local.sh [options]

Runs the standard laptop-only validation path for the Neutron 2 F Prime repo.

Default checks:
  - generated transport headers match config/transport_constants.json
  - F Prime/Teensy transport constants agree
  - Python local-emulation and payload-receiver unit tests pass
  - local-demo topology generates and builds
  - F Prime component unit tests pass
  - automated local Neutron 2 demo sequence produces and parses a science CSV

Options:
  --skip-build     skip F Prime local-demo generate/build
  --skip-ut        skip F Prime component unit tests
  --skip-demo      skip automated local demo run
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

log "checking generated transport headers"
python3 tools/generate_transport_constants.py --check
python3 tools/check_transport_constants.py

log "running Python local-emulation tests"
python3 -m unittest \
  ArtemisRpiTeensy_N2/tools/tests/test_local_emulation_loop.py \
  ArtemisRpiTeensy_N2/tools/tests/test_payload_receiver.py

# shellcheck disable=SC1090
. "$VENV_ACTIVATE"

if [[ "$RUN_BUILD" == "true" ]]; then
  log "building local-demo topology"
  (
    cd "$FPRIME_ROOT"
    fprime-util generate -f -DNEUTRON2_TOPOLOGY_PROFILE=local-demo
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
  log "running automated local demo sequence"
  (
    cd "$FPRIME_ROOT"
    ./tools/run_neutron2_local_demo.sh \
      --skip-build \
      --exit-after-sequence \
      --gui-port 5061 \
      --viewer-port 8063 \
      --delay 2 \
      --capture-seconds 2
  )
fi

log "PASS"
