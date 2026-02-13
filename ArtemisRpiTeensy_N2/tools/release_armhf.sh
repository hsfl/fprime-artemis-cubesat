#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PI_USER=""
PI_HOST=""
REMOTE_BASE=""
RELEASE_DIR=""
RELEASE_NAME=""
JOBS=""
PULL_IMAGE="false"
SKIP_BUILD="false"
SKIP_DEPLOY="false"
SKIP_SMOKE="false"
UART_DEVICE="/dev/serial0"
DURATION_SECONDS="30"

usage() {
  cat <<'EOF'
Usage: release_armhf.sh [options]

Run the armhf golden pipeline: cross-build -> package -> deploy -> smoke-test.

Options:
  --pi-user <user>          SSH username for deploy/smoke steps
  --pi-host <host>          SSH host/IP for deploy/smoke steps
  --remote-base <path>      Remote base directory (default: /home/<user>/artemis)
  --release-dir <path>      Reuse an existing packaged release (skip package creation)
  --release-name <name>     Release directory name when packaging
  --jobs <N>                Build with N parallel jobs
  --pull                    Pull Docker image before cross-build
  --skip-build              Skip cross-build step
  --skip-deploy             Skip deploy step
  --skip-smoke              Skip smoke-test step
  --uart-device <path>      UART passed to deployment (default: /dev/serial0)
  --duration <seconds>      Smoke duration (default: 30)
  -h, --help                Show this help text
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
    --release-dir)
      RELEASE_DIR="${2:-}"
      shift 2
      ;;
    --release-name)
      RELEASE_NAME="${2:-}"
      shift 2
      ;;
    --jobs)
      JOBS="${2:-}"
      shift 2
      ;;
    --pull)
      PULL_IMAGE="true"
      shift
      ;;
    --skip-build)
      SKIP_BUILD="true"
      shift
      ;;
    --skip-deploy)
      SKIP_DEPLOY="true"
      shift
      ;;
    --skip-smoke)
      SKIP_SMOKE="true"
      shift
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

if [[ "${SKIP_DEPLOY}" != "true" || "${SKIP_SMOKE}" != "true" ]]; then
  if [[ -z "${PI_USER}" || -z "${PI_HOST}" ]]; then
    echo "--pi-user and --pi-host are required unless both --skip-deploy and --skip-smoke are set" >&2
    exit 1
  fi
fi

if [[ -z "${REMOTE_BASE}" && -n "${PI_USER}" ]]; then
  REMOTE_BASE="/home/${PI_USER}/artemis"
fi

if [[ -z "${RELEASE_DIR}" ]]; then
  if [[ "${SKIP_BUILD}" != "true" ]]; then
    BUILD_CMD=("${ROOT_DIR}/tools/cross_build_armhf.sh")
    if [[ "${PULL_IMAGE}" == "true" ]]; then
      BUILD_CMD+=(--pull)
    fi
    if [[ -n "${JOBS}" ]]; then
      BUILD_CMD+=(--jobs "${JOBS}")
    fi
    "${BUILD_CMD[@]}"
  fi

  PACKAGE_CMD=("${ROOT_DIR}/tools/package_armhf_release.sh" --print-path-only)
  if [[ -n "${RELEASE_NAME}" ]]; then
    PACKAGE_CMD+=(--release-name "${RELEASE_NAME}")
  fi
  RELEASE_DIR="$("${PACKAGE_CMD[@]}")"
fi

echo "Using release directory: ${RELEASE_DIR}"

if [[ "${SKIP_DEPLOY}" != "true" ]]; then
  DEPLOY_CMD=(
    "${ROOT_DIR}/tools/deploy_armhf_release.sh"
    --release-dir "${RELEASE_DIR}"
    --pi-user "${PI_USER}"
    --pi-host "${PI_HOST}"
  )
  if [[ -n "${REMOTE_BASE}" ]]; then
    DEPLOY_CMD+=(--remote-base "${REMOTE_BASE}")
  fi
  "${DEPLOY_CMD[@]}"
fi

if [[ "${SKIP_SMOKE}" != "true" ]]; then
  SMOKE_CMD=(
    "${ROOT_DIR}/tools/smoke_test_pi_release.sh"
    --pi-user "${PI_USER}"
    --pi-host "${PI_HOST}"
    --uart-device "${UART_DEVICE}"
    --duration "${DURATION_SECONDS}"
  )
  if [[ -n "${REMOTE_BASE}" ]]; then
    SMOKE_CMD+=(--remote-base "${REMOTE_BASE}")
  fi
  "${SMOKE_CMD[@]}"
fi

echo "Pipeline completed."

