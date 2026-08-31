#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${PORT_ROOT}/.." && pwd)"
DEVELOPER_ROOT="$(cd "${REPO_ROOT}/.." && pwd)"

export ZEPHYR_BASE="${ZEPHYR_BASE:-${DEVELOPER_ROOT}/fprime-zephyr-reference/lib/zephyr-workspace/zephyr}"
export FPRIME_ZEPHYR_WORKSPACE="${FPRIME_ZEPHYR_WORKSPACE:-${DEVELOPER_ROOT}/fprime-zephyr-reference/lib/zephyr-workspace}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-${DEVELOPER_ROOT}/zephyr-sdk-0.17.4}"
export PATH="${REPO_ROOT}/ArtemisRpiTeensy_N2/fprime-venv/bin:${PATH}"

cd "${PORT_ROOT}/fprime"
fprime-util generate -f zephyr
# This checked-out F Prime release has parallel autocoder dependency races.
# Build the deployable target serially; the full default target also pulls in
# unrelated framework test helpers that are not part of this deployment.
ninja -C build-fprime-automatic-zephyr -j1 zephyr.elf

