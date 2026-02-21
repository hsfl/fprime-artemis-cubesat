#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_ACTIVATE="$ROOT_DIR/fprime-venv/bin/activate"
EMU_SCRIPT="$ROOT_DIR/tools/local_emulation_loop.py"

if [[ ! -f "$VENV_ACTIVATE" ]]; then
  echo "Missing F' virtual environment activate script: $VENV_ACTIVATE" >&2
  exit 1
fi

if [[ ! -x "$EMU_SCRIPT" ]]; then
  chmod +x "$EMU_SCRIPT"
fi

# shellcheck disable=SC1090
. "$VENV_ACTIVATE"

exec "$EMU_SCRIPT" "$@"
