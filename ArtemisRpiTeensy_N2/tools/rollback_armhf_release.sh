#!/usr/bin/env bash
set -euo pipefail

DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
PI_USER=""
PI_HOST=""
REMOTE_BASE=""

usage() {
  cat <<'EOF'
Usage: rollback_armhf_release.sh --pi-user <user> --pi-host <host> [options]

Rollback active release on Raspberry Pi to the previously active release.

Options:
  --pi-user <user>         SSH username
  --pi-host <host>         SSH host/IP
  --remote-base <path>     Remote base directory (default: /home/<user>/artemis)
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

SSH_TARGET="${PI_USER}@${PI_HOST}"

ssh "${SSH_TARGET}" "set -euo pipefail
REMOTE_BASE='${REMOTE_BASE}'
DEPLOYMENT_NAME='${DEPLOYMENT_NAME}'
ACTIVE_LINK=\"\${REMOTE_BASE}/active_release\"
PREVIOUS_LINK=\"\${REMOTE_BASE}/previous_release\"
CURRENT_DIR=\"\${REMOTE_BASE}/current\"

if [[ ! -L \"\${PREVIOUS_LINK}\" ]]; then
  echo \"No previous release link found at \${PREVIOUS_LINK}\" >&2
  exit 1
fi

PREVIOUS_TARGET=\"\$(readlink \"\${PREVIOUS_LINK}\")\"
if [[ ! -d \"\${PREVIOUS_TARGET}\" ]]; then
  echo \"Previous release target does not exist: \${PREVIOUS_TARGET}\" >&2
  exit 1
fi

CURRENT_TARGET=''
if [[ -L \"\${ACTIVE_LINK}\" ]]; then
  CURRENT_TARGET=\"\$(readlink \"\${ACTIVE_LINK}\")\"
fi

ln -sfn \"\${PREVIOUS_TARGET}\" \"\${ACTIVE_LINK}\"
if [[ -n \"\${CURRENT_TARGET}\" ]]; then
  ln -sfn \"\${CURRENT_TARGET}\" \"\${PREVIOUS_LINK}\"
fi

ln -sfn \"\${ACTIVE_LINK}/\${DEPLOYMENT_NAME}\" \"\${CURRENT_DIR}/\${DEPLOYMENT_NAME}\"
ln -sfn \"\${ACTIVE_LINK}/\${DEPLOYMENT_NAME}TopologyDictionary.json\" \"\${CURRENT_DIR}/\${DEPLOYMENT_NAME}TopologyDictionary.json\"
echo \"Rollback complete. Active release: \$(readlink \${ACTIVE_LINK})\"
"

