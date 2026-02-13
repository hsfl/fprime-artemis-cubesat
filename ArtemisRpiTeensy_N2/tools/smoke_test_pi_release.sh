#!/usr/bin/env bash
set -euo pipefail

DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
PI_USER=""
PI_HOST=""
REMOTE_BASE=""
UART_DEVICE="/dev/serial0"
DURATION_SECONDS="30"

usage() {
  cat <<'EOF'
Usage: smoke_test_pi_release.sh --pi-user <user> --pi-host <host> [options]

Run deployment on Raspberry Pi for a fixed interval and pass only if it remains alive for full duration.

Options:
  --pi-user <user>         SSH username
  --pi-host <host>         SSH host/IP
  --remote-base <path>     Remote base directory (default: /home/<user>/artemis)
  --uart-device <path>     UART path passed with -d (default: /dev/serial0)
  --duration <seconds>     Required alive duration (default: 30)
  -h, --help               Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pi-user)
      PI_USER="${2:-}"
      shift 2
      ;;
    --pi-host)
      PI_HOST="${2:-}"
      shift 2
      ;;
    --remote-base)
      REMOTE_BASE="${2:-}"
      shift 2
      ;;
    --uart-device)
      UART_DEVICE="${2:-}"
      shift 2
      ;;
    --duration)
      DURATION_SECONDS="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "${PI_USER}" || -z "${PI_HOST}" ]]; then
  usage >&2
  exit 1
fi

if [[ -z "${REMOTE_BASE}" ]]; then
  REMOTE_BASE="/home/${PI_USER}/artemis"
fi

if [[ ! "${DURATION_SECONDS}" =~ ^[0-9]+$ ]]; then
  echo "--duration must be a positive integer (seconds)" >&2
  exit 1
fi

SSH_TARGET="${PI_USER}@${PI_HOST}"

ssh "${SSH_TARGET}" "set -euo pipefail
REMOTE_BASE='${REMOTE_BASE}'
DEPLOYMENT_NAME='${DEPLOYMENT_NAME}'
UART_DEVICE='${UART_DEVICE}'
DURATION_SECONDS='${DURATION_SECONDS}'
APP=\"\${REMOTE_BASE}/current/\${DEPLOYMENT_NAME}\"
LOG_DIR=\"\${REMOTE_BASE}/logs\"
mkdir -p \"\${LOG_DIR}\"
LOG_FILE=\"\${LOG_DIR}/smoke-\$(date -u +%Y%m%d-%H%M%S).log\"

if [[ ! -x \"\${APP}\" ]]; then
  echo \"Deployment binary is missing or not executable: \${APP}\" >&2
  exit 1
fi

if ! command -v timeout >/dev/null 2>&1; then
  echo \"Missing required command on target: timeout\" >&2
  exit 2
fi

set +e
timeout \"\${DURATION_SECONDS}s\" \"\${APP}\" -d \"\${UART_DEVICE}\" >\"\${LOG_FILE}\" 2>&1
RC=\$?
set -e

echo \"Smoke test log: \${LOG_FILE}\"
tail -n 40 \"\${LOG_FILE}\" || true

if [[ \"\${RC}\" -eq 124 ]]; then
  echo \"PASS: deployment remained alive for \${DURATION_SECONDS} seconds\"
  exit 0
fi

echo \"FAIL: deployment exited before \${DURATION_SECONDS} seconds (rc=\${RC})\" >&2
exit \"\${RC}\"
"

