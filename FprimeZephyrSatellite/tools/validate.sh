#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${PORT_ROOT}/.." && pwd)"
DEVELOPER_ROOT="$(cd "${REPO_ROOT}/.." && pwd)"
HOST_BUILD="${TMPDIR:-/tmp}/fprime-zephyr-satellite-host-build"
ZEPHYR_BUILD="${TMPDIR:-/tmp}/fprime-zephyr-satellite-teensy41-build"

python3 "${REPO_ROOT}/tools/generate_transport_constants.py" --check
python3 "${REPO_ROOT}/tools/check_transport_constants.py"

cmake -S "${PORT_ROOT}" -B "${HOST_BUILD}" -G Ninja
cmake --build "${HOST_BUILD}"
ctest --test-dir "${HOST_BUILD}" --output-on-failure

export ZEPHYR_BASE="${ZEPHYR_BASE:-${DEVELOPER_ROOT}/fprime-zephyr-reference/lib/zephyr-workspace/zephyr}"
export FPRIME_ZEPHYR_WORKSPACE="${FPRIME_ZEPHYR_WORKSPACE:-${DEVELOPER_ROOT}/fprime-zephyr-reference/lib/zephyr-workspace}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-${DEVELOPER_ROOT}/zephyr-sdk-0.17.4}"

"${REPO_ROOT}/ArtemisRpiTeensy_N2/fprime-venv/bin/west" build -p always \
  -b teensy41 -s "${PORT_ROOT}/zephyr" -d "${ZEPHYR_BUILD}"

"${SCRIPT_DIR}/build_teensy41.sh"

