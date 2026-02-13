#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_MOUNT_DEFAULT="$(cd "${ROOT_DIR}/.." && pwd)"

IMAGE="nasafprime/fprime-arm:latest"
REPO_MOUNT="${REPO_MOUNT_DEFAULT}"
PULL_IMAGE="false"
DRY_RUN="false"
JOBS=""

usage() {
  cat <<'EOF'
Usage: cross_build_armhf.sh [options]

Cross-compile ArtemisRpiTeensyDeployment for arm-hf-linux in the official F' ARM Docker image.

Options:
  --pull                Pull Docker image before building
  --image <name>        Docker image (default: nasafprime/fprime-arm:latest)
  --repo-mount <path>   Host path mounted to /project (default: repo root)
  --jobs <N>            Build with N parallel jobs
  --dry-run             Print docker command and exit
  -h, --help            Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pull)
      PULL_IMAGE="true"
      shift
      ;;
    --image)
      IMAGE="${2:-}"
      shift 2
      ;;
    --repo-mount)
      REPO_MOUNT="${2:-}"
      shift 2
      ;;
    --jobs)
      JOBS="${2:-}"
      shift 2
      ;;
    --dry-run)
      DRY_RUN="true"
      shift
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

if [[ ! -d "${REPO_MOUNT}" ]]; then
  echo "Repository mount path does not exist: ${REPO_MOUNT}" >&2
  exit 1
fi

if [[ -n "${JOBS}" && ! "${JOBS}" =~ ^[0-9]+$ ]]; then
  echo "--jobs must be a positive integer" >&2
  exit 1
fi

if [[ "${PULL_IMAGE}" == "true" ]]; then
  docker pull "${IMAGE}"
fi

if [[ -n "${JOBS}" ]]; then
  BUILD_STEP="fprime-util build arm-hf-linux -j ${JOBS}"
else
  BUILD_STEP="fprime-util build arm-hf-linux"
fi

read -r -d '' CONTAINER_CMD <<EOF || true
set -euo pipefail
cd /project/ArtemisRpiTeensy_N2

TOOLS_VERSION="\$(awk -F'==' '/^fprime-tools==/{print \$2; exit}' lib/fprime/requirements.txt)"
FPP_VERSION="\$(awk -F'==' '/^fprime-fpp==/{print \$2; exit}' lib/fprime/requirements.txt)"
if [[ -z "\${TOOLS_VERSION}" || -z "\${FPP_VERSION}" ]]; then
  echo "Unable to read fprime tool versions from lib/fprime/requirements.txt" >&2
  exit 1
fi

TOOLS_VENV="/opt/fprime-tools-cache/fprime-cross-tools-\${TOOLS_VERSION}"
if [[ ! -x "\${TOOLS_VENV}/bin/fprime-util" ]]; then
  mkdir -p /opt/fprime-tools-cache
  python3 -m venv "\${TOOLS_VENV}"
  . "\${TOOLS_VENV}/bin/activate"
  python -m pip install --upgrade pip
  python -m pip install "fprime-tools==\${TOOLS_VERSION}" "fprime-fpp==\${FPP_VERSION}"
else
  . "\${TOOLS_VENV}/bin/activate"
fi

export ARM_TOOLS_PATH=/opt/toolchains
fprime-util generate -f --make arm-hf-linux
${BUILD_STEP}
EOF

DOCKER_CMD=(
  docker run --rm
  --platform=linux/amd64
  --net host
  -u "$(id -u):$(id -g)"
  -v "${REPO_MOUNT}:/project"
  -v "fprime-arm-tools-cache:/opt/fprime-tools-cache"
  --entrypoint /bin/bash
  "${IMAGE}"
  -lc "${CONTAINER_CMD}"
)

if [[ "${DRY_RUN}" == "true" ]]; then
  printf '%q ' "${DOCKER_CMD[@]}"
  printf '\n'
  exit 0
fi

printf '%q ' "${DOCKER_CMD[@]}"
printf '\n'
"${DOCKER_CMD[@]}"
