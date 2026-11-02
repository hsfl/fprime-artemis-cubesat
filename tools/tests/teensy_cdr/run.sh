#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SRC="$ROOT/ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src"
TASK_BUILD="$(mktemp -d)"
trap 'rm -rf "$TASK_BUILD"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-private-field \
  -DARTEMIS_CDR_NO_RADIO=1 -D__IMXRT1062__ \
  -I"$HERE" -I"$SRC" -I"$ROOT/external/artemis-pdu/src" \
  "$HERE/smoke.cpp" "$SRC/relay_uart_rf.cpp" \
  "$SRC/local_teensy_router.cpp" "$SRC/pdu_proxy.cpp" \
  -o "$TASK_BUILD/teensy-cdr-smoke"
"$TASK_BUILD/teensy-cdr-smoke"
