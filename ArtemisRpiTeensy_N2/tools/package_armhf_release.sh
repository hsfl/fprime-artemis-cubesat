#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "${ROOT_DIR}/.." && pwd)"

DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
PLATFORM="arm-hf-linux"
OUTPUT_BASE="${ROOT_DIR}/releases"
RELEASE_NAME=""
PRINT_PATH_ONLY="false"
DRY_RUN="false"

usage() {
  cat <<'EOF'
Usage: package_armhf_release.sh [options]

Package arm-hf-linux deployment artifacts into a release directory with checksums and metadata.

Options:
  --output-base <path>      Base output directory (default: ArtemisRpiTeensy_N2/releases)
  --release-name <name>     Explicit release directory name
  --print-path-only         Print release path only (for scripting)
  --dry-run                 Print resolved paths and exit
  -h, --help                Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output-base)
      OUTPUT_BASE="${2:-}"
      shift 2
      ;;
    --release-name)
      RELEASE_NAME="${2:-}"
      shift 2
      ;;
    --print-path-only)
      PRINT_PATH_ONLY="true"
      shift
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

SOURCE_BASE="${ROOT_DIR}/build-artifacts/${PLATFORM}/${DEPLOYMENT_NAME}"
BINARY_PATH="${SOURCE_BASE}/bin/${DEPLOYMENT_NAME}"
DICT_PATH="${SOURCE_BASE}/dict/${DEPLOYMENT_NAME}TopologyDictionary.json"

if [[ ! -f "${BINARY_PATH}" ]]; then
  echo "Missing binary artifact: ${BINARY_PATH}" >&2
  echo "Run cross build first: ./tools/cross_build_armhf.sh" >&2
  exit 1
fi

if [[ ! -f "${DICT_PATH}" ]]; then
  echo "Missing dictionary artifact: ${DICT_PATH}" >&2
  echo "Run cross build first: ./tools/cross_build_armhf.sh" >&2
  exit 1
fi

BINARY_FILE_INFO="$(file "${BINARY_PATH}")"
if ! printf '%s\n' "${BINARY_FILE_INFO}" | grep -Eiq 'arm'; then
  echo "Binary does not appear to target ARM: ${BINARY_FILE_INFO}" >&2
  exit 1
fi

GIT_SHA="$(git -C "${REPO_ROOT}" rev-parse --short HEAD 2>/dev/null || echo "nogit")"
TIMESTAMP_UTC="$(date -u +%Y%m%d-%H%M%S)"

if [[ -z "${RELEASE_NAME}" ]]; then
  RELEASE_NAME="release-${TIMESTAMP_UTC}-${GIT_SHA}-armhf"
fi

RELEASE_DIR="${OUTPUT_BASE}/${RELEASE_NAME}"

if [[ "${DRY_RUN}" == "true" ]]; then
  cat <<EOF
binary:      ${BINARY_PATH}
dictionary:  ${DICT_PATH}
release_dir: ${RELEASE_DIR}
git_sha:     ${GIT_SHA}
EOF
  exit 0
fi

mkdir -p "${RELEASE_DIR}"

cp "${BINARY_PATH}" "${RELEASE_DIR}/${DEPLOYMENT_NAME}"
cp "${DICT_PATH}" "${RELEASE_DIR}/${DEPLOYMENT_NAME}TopologyDictionary.json"

cat > "${RELEASE_DIR}/RELEASE_INFO.txt" <<EOF
deployment=${DEPLOYMENT_NAME}
platform=${PLATFORM}
timestamp_utc=${TIMESTAMP_UTC}
git_sha=${GIT_SHA}
source_binary=${BINARY_PATH}
source_dictionary=${DICT_PATH}
binary_file_info=${BINARY_FILE_INFO}
EOF

if command -v sha256sum >/dev/null 2>&1; then
  (
    cd "${RELEASE_DIR}"
    sha256sum "${DEPLOYMENT_NAME}" "${DEPLOYMENT_NAME}TopologyDictionary.json" > SHA256SUMS
  )
else
  (
    cd "${RELEASE_DIR}"
    shasum -a 256 "${DEPLOYMENT_NAME}" "${DEPLOYMENT_NAME}TopologyDictionary.json" > SHA256SUMS
  )
fi

chmod +x "${RELEASE_DIR}/${DEPLOYMENT_NAME}"

if [[ "${PRINT_PATH_ONLY}" == "true" ]]; then
  printf '%s\n' "${RELEASE_DIR}"
else
  cat <<EOF
Created release package:
  ${RELEASE_DIR}

Contents:
  ${RELEASE_DIR}/${DEPLOYMENT_NAME}
  ${RELEASE_DIR}/${DEPLOYMENT_NAME}TopologyDictionary.json
  ${RELEASE_DIR}/SHA256SUMS
  ${RELEASE_DIR}/RELEASE_INFO.txt
EOF
fi

