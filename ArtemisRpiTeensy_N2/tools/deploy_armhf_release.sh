#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
RELEASE_DIR=""
PI_USER=""
PI_HOST=""
REMOTE_BASE=""

usage() {
  cat <<'EOF'
Usage: deploy_armhf_release.sh --release-dir <path> --pi-user <user> --pi-host <host> [options]

Copy a packaged armhf release to Raspberry Pi and atomically activate it.

Options:
  --release-dir <path>     Local release directory from package_armhf_release.sh
  --pi-user <user>         SSH username
  --pi-host <host>         SSH host/IP
  --remote-base <path>     Remote base directory (default: /home/<user>/artemis)
  -h, --help               Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --release-dir)
      RELEASE_DIR="${2:-}"
      shift 2
      ;;
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

if [[ -z "${RELEASE_DIR}" || -z "${PI_USER}" || -z "${PI_HOST}" ]]; then
  usage >&2
  exit 1
fi

if [[ -z "${REMOTE_BASE}" ]]; then
  REMOTE_BASE="/home/${PI_USER}/artemis"
fi

if [[ ! -d "${RELEASE_DIR}" ]]; then
  echo "Release directory not found: ${RELEASE_DIR}" >&2
  exit 1
fi

if [[ ! -f "${RELEASE_DIR}/${DEPLOYMENT_NAME}" ]]; then
  echo "Missing release binary in ${RELEASE_DIR}" >&2
  exit 1
fi

if [[ ! -f "${RELEASE_DIR}/${DEPLOYMENT_NAME}TopologyDictionary.json" ]]; then
  echo "Missing release dictionary in ${RELEASE_DIR}" >&2
  exit 1
fi

if [[ ! -f "${RELEASE_DIR}/SHA256SUMS" ]]; then
  echo "Missing SHA256SUMS in ${RELEASE_DIR}" >&2
  exit 1
fi

RELEASE_NAME="$(basename "${RELEASE_DIR}")"
REMOTE_RELEASES_DIR="${REMOTE_BASE}/releases"
REMOTE_RELEASE_DIR="${REMOTE_RELEASES_DIR}/${RELEASE_NAME}"
SSH_TARGET="${PI_USER}@${PI_HOST}"

ssh "${SSH_TARGET}" "mkdir -p '${REMOTE_RELEASES_DIR}' '${REMOTE_BASE}/current'"
scp -r "${RELEASE_DIR}" "${SSH_TARGET}:${REMOTE_RELEASES_DIR}/"

ssh "${SSH_TARGET}" "set -euo pipefail
REMOTE_BASE='${REMOTE_BASE}'
DEPLOYMENT_NAME='${DEPLOYMENT_NAME}'
RELEASE_DIR='${REMOTE_RELEASE_DIR}'
ACTIVE_LINK=\"\${REMOTE_BASE}/active_release\"
PREVIOUS_LINK=\"\${REMOTE_BASE}/previous_release\"
CURRENT_DIR=\"\${REMOTE_BASE}/current\"

if [[ ! -d \"\${RELEASE_DIR}\" ]]; then
  echo \"Remote release directory missing: \${RELEASE_DIR}\" >&2
  exit 1
fi

if [[ -L \"\${ACTIVE_LINK}\" ]]; then
  CURRENT_TARGET=\"\$(readlink \"\${ACTIVE_LINK}\")\"
  ln -sfn \"\${CURRENT_TARGET}\" \"\${PREVIOUS_LINK}\"
fi

chmod +x \"\${RELEASE_DIR}/\${DEPLOYMENT_NAME}\"
ln -sfn \"\${RELEASE_DIR}\" \"\${ACTIVE_LINK}\"
ln -sfn \"\${ACTIVE_LINK}/\${DEPLOYMENT_NAME}\" \"\${CURRENT_DIR}/\${DEPLOYMENT_NAME}\"
ln -sfn \"\${ACTIVE_LINK}/\${DEPLOYMENT_NAME}TopologyDictionary.json\" \"\${CURRENT_DIR}/\${DEPLOYMENT_NAME}TopologyDictionary.json\"
echo '${RELEASE_NAME}' > \"\${REMOTE_BASE}/active_release_name.txt\"
echo \"Activated release: ${RELEASE_NAME}\"
echo \"Current binary: \${CURRENT_DIR}/\${DEPLOYMENT_NAME}\"
"

cat <<EOF
Deployed and activated release:
  local:  ${RELEASE_DIR}
  remote: ${REMOTE_RELEASE_DIR}
  host:   ${SSH_TARGET}
EOF

