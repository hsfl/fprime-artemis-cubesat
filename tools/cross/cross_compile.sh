#!/usr/bin/env bash
set -euo pipefail

# Repo-wide ARM cross-compile driver.
#
# Builds any F Prime deployment in this repo for a Raspberry Pi target inside
# Docker, verifies the binary really is ARMv6-safe, optionally copies it to the
# Pi, and runs a /dev/null smoke test there.

CROSS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$CROSS_DIR/../.." && pwd)"

TOOLCHAIN="pi-zero-w-armv6hf"
IMAGE_TAG="artemis-pi-cross:local"

PROJECT="${ARTEMIS_CROSS_PROJECT:-ArtemisRpiTeensy_N2}"
DEPLOYMENT="${ARTEMIS_CROSS_DEPLOYMENT:-ArtemisRpiTeensyDeployment}"

HOST="${PI_SSH_HOST:-${PI_ZERO_W_SSH_HOST:-pi@raspberrypi-zero-w}}"
SYSROOT_DIR="${PI_SYSROOT_DIR:-${PI_ZERO_W_SYSROOT_DIR:-$CROSS_DIR/sysroot}}"
REMOTE_DIR="${PI_REMOTE_DIR:-${PI_ZERO_W_REMOTE_DIR:-/home/pi/artemis/cross}}"

SYNC_SYSROOT="auto"
BUILD_IMAGE="auto"
CLEAN="false"
LOCAL_ONLY="false"
COPY_ONLY="false"
SKIP_SMOKE="false"

usage() {
  cat <<'EOF'
Usage: tools/cross/cross_compile.sh [options]

Cross-compile an F Prime deployment for the Raspberry Pi Zero W (ARMv6 hard
float) inside Docker, verify the ARM attributes, then optionally deploy and
smoke test on the Pi.

Selecting what to build:
  --project <dir>       F Prime project directory, relative to the repo root
                        (default: ArtemisRpiTeensy_N2)
  --deployment <name>   Deployment directory name inside that project
                        (default: ArtemisRpiTeensyDeployment)

The deployment directory is located by name anywhere under the project, so both
flat layouts (ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment) and nested ones
(fprime-artemis-core/FprimeArtemisCore/Deployments/PayloadComputerDeployment)
work without extra flags.

Target setup (change these for your own Pi before the first run):
  1. SSH target: export PI_SSH_HOST or pass --host
  2. Remote directory: export PI_REMOTE_DIR or pass --remote-dir
  3. Optional sysroot location: export PI_SYSROOT_DIR or pass --sysroot

Examples:
  # Default RPi deployment, build + ARMv6 verify only, no Pi needed.
  ./tools/cross/cross_compile.sh --local-only

  # A deployment from the fprime-artemis-core project.
  ./tools/cross/cross_compile.sh --local-only \
      --project fprime-artemis-core \
      --deployment PayloadComputerDeployment

  # Full flow: build, verify, copy to the Pi, smoke test.
  export PI_SSH_HOST=pi@192.168.1.44
  ./tools/cross/cross_compile.sh

  # Full refresh of image, venv, and build cache (keeps the shared sysroot).
  ./tools/cross/cross_compile.sh --clean

Options:
  --project <dir>       Project directory (default: ArtemisRpiTeensy_N2)
  --deployment <name>   Deployment name (default: ArtemisRpiTeensyDeployment)
  --host <ssh-host>     SSH host alias or user@host
  --sysroot <path>      Sysroot directory (default: tools/cross/sysroot)
  --remote-dir <path>   Remote deploy directory on the Pi
  --clean               Refresh Docker image, cross venv, and F Prime build
                        cache for the selected project. Leaves the shared
                        sysroot alone.
  --resync-sysroot      Re-copy the sysroot from the Pi. The sysroot is shared
                        by every project, so this affects all of them.
  --skip-sync           Reuse an existing sysroot without rsync
  --skip-image-build    Reuse the existing Docker image tag
  --local-only          Build + verify locally only (skip SSH deploy/smoke)
  --skip-smoke          Deploy to the Pi but do not run the remote smoke test.
                        Use when the deployment is managed by systemd, or when
                        a run would contend for a device the service holds.
  --copy-only           Skip sync/build; deploy the previously verified binary
                        for this project/deployment and smoke test only
  -h, --help            Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --project)
      PROJECT="${2:-}"
      shift 2
      ;;
    --deployment)
      DEPLOYMENT="${2:-}"
      shift 2
      ;;
    --host)
      HOST="${2:-}"
      shift 2
      ;;
    --sysroot)
      SYSROOT_DIR="${2:-}"
      shift 2
      ;;
    --remote-dir)
      REMOTE_DIR="${2:-}"
      shift 2
      ;;
    --clean)
      CLEAN="true"
      shift
      ;;
    --skip-sync)
      SYNC_SYSROOT="false"
      shift
      ;;
    --resync-sysroot)
      SYNC_SYSROOT="true"
      shift
      ;;
    --skip-image-build)
      BUILD_IMAGE="false"
      shift
      ;;
    --local-only)
      LOCAL_ONLY="true"
      shift
      ;;
    --copy-only)
      COPY_ONLY="true"
      shift
      ;;
    --skip-smoke)
      SKIP_SMOKE="true"
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

if [[ "$COPY_ONLY" == "true" && "$LOCAL_ONLY" == "true" ]]; then
  echo "--copy-only and --local-only are mutually exclusive" >&2
  exit 2
fi

# Strip a leading ./ and any trailing slash so the project name is stable when
# it is reused as a path component below.
PROJECT="${PROJECT#./}"
PROJECT="${PROJECT%/}"
PROJECT_DIR="$REPO_ROOT/$PROJECT"

if [[ ! -d "$PROJECT_DIR" ]]; then
  echo "Project directory not found: $PROJECT_DIR" >&2
  exit 2
fi

if [[ ! -f "$PROJECT_DIR/settings.ini" ]]; then
  echo "Not an F Prime project (no settings.ini): $PROJECT_DIR" >&2
  exit 2
fi

if [[ ! -d "$PROJECT_DIR/lib/fprime/cmake/toolchain/helpers" ]]; then
  echo "Project is missing the F Prime framework submodule: $PROJECT_DIR/lib/fprime" >&2
  echo "Run: git submodule update --init --recursive" >&2
  exit 2
fi

# Locate the deployment by name so flat and nested layouts both work. Build
# caches and artifact trees are pruned to avoid matching generated copies.
DEPLOYMENT_DIR="$(find "$PROJECT_DIR" \
  \( -path "*/lib/*" -o -path "*/build-*" -o -path "*/fprime-venv/*" \) -prune -o \
  -type d -name "$DEPLOYMENT" -print 2>/dev/null | head -n 1)"

if [[ -z "$DEPLOYMENT_DIR" ]]; then
  echo "Deployment '$DEPLOYMENT' not found under $PROJECT_DIR" >&2
  echo "Available deployments:" >&2
  find "$PROJECT_DIR" \
    \( -path "*/lib/*" -o -path "*/build-*" -o -path "*/fprime-venv/*" \) -prune -o \
    -type d -name "*Deployment" -print 2>/dev/null | sed "s|^$PROJECT_DIR/|  |" >&2
  exit 2
fi

# Path of the deployment relative to the project, so the container can find it
# at the same spot under its own mount point.
DEPLOYMENT_REL="${DEPLOYMENT_DIR#"$PROJECT_DIR"/}"

# Verification output is kept per project and deployment so that --copy-only
# can never deploy a binary belonging to a different target.
VERIFY_DIR="$CROSS_DIR/verify/$PROJECT/$DEPLOYMENT"
mkdir -p "$VERIFY_DIR"

echo "Cross-build target configuration"
echo "  project: $PROJECT"
echo "  deployment: $DEPLOYMENT ($DEPLOYMENT_REL)"
echo "  toolchain: $TOOLCHAIN"
echo "  ssh host: $HOST"
echo "  sysroot: $SYSROOT_DIR"
echo "  remote dir: $REMOTE_DIR"
echo "  docker image: $IMAGE_TAG"
echo "  verify dir: $VERIFY_DIR"
echo "  clean: $CLEAN"
echo "  local only: $LOCAL_ONLY"
echo "  copy only: $COPY_ONLY"
echo "  skip smoke: $SKIP_SMOKE"
echo

if [[ "$COPY_ONLY" != "true" ]]; then

# --clean rebuilds this project's image, venv, and build cache. It deliberately
# does NOT re-sync the sysroot: that directory is now shared by every project
# here, so a --clean on one target would otherwise discard the sysroot every
# other target depends on. Use --resync-sysroot when you actually want it
# refreshed from the Pi.
if [[ "$CLEAN" == "true" ]]; then
  if [[ "$BUILD_IMAGE" == "auto" ]]; then
    BUILD_IMAGE="true"
  fi
fi

if [[ "$LOCAL_ONLY" == "true" && "$SYNC_SYSROOT" == "auto" ]]; then
  echo "Local-only mode selected; reusing existing sysroot"
  SYNC_SYSROOT="false"
fi

if [[ "$SYNC_SYSROOT" == "auto" ]]; then
  if [[ -d "$SYSROOT_DIR/usr/lib/arm-linux-gnueabihf" &&
        -e "$SYSROOT_DIR/lib/ld-linux-armhf.so.3" &&
        -d "$SYSROOT_DIR/usr/lib/gcc/arm-linux-gnueabihf" ]]; then
    echo "Reusing existing Pi sysroot"
    SYNC_SYSROOT="false"
  else
    echo "Pi sysroot is missing or incomplete; syncing from target"
    SYNC_SYSROOT="true"
  fi
fi

if [[ "$SYNC_SYSROOT" == "true" ]]; then
  "$CROSS_DIR/sync_sysroot.sh" --host "$HOST" --dest "$SYSROOT_DIR"
fi

if [[ ! -d "$SYSROOT_DIR/usr/lib/arm-linux-gnueabihf" ]]; then
  echo "Sysroot is missing expected armhf libraries: $SYSROOT_DIR/usr/lib/arm-linux-gnueabihf" >&2
  exit 1
fi

if [[ ! -e "$SYSROOT_DIR/lib/ld-linux-armhf.so.3" ]]; then
  echo "Sysroot is missing the ARM hard-float loader: $SYSROOT_DIR/lib/ld-linux-armhf.so.3" >&2
  exit 1
fi

if [[ ! -d "$SYSROOT_DIR/usr/lib/gcc/arm-linux-gnueabihf" ]]; then
  echo "Sysroot is missing Pi GCC runtime objects: $SYSROOT_DIR/usr/lib/gcc/arm-linux-gnueabihf" >&2
  exit 1
fi

# The Pi's own compiler helpers are 32-bit ARM executables. They sit in a
# directory the toolchain passes to gcc as -B, and gcc searches -B paths for
# programs as well as libraries, so leaving them here makes the cross gcc run
# the Pi's cc1 under qemu. sync_sysroot.sh deletes them; a sysroot copied by
# hand from another machine may still have them.
STRAY_HELPERS="$(find "$SYSROOT_DIR/usr/lib/gcc/arm-linux-gnueabihf" -type f \
  \( -name cc1 -o -name cc1plus -o -name collect2 -o -name lto1 \
     -o -name lto-wrapper -o -name g++-mapper-server \
     -o -name 'liblto_plugin.so*' \) 2>/dev/null | head -n 5)"

if [[ -n "$STRAY_HELPERS" ]]; then
  echo "Sysroot still contains the Pi's own ARM compiler helpers:" >&2
  printf '  %s\n' $STRAY_HELPERS >&2
  echo "These make the cross compiler fail with 'qemu-arm: Could not open ld-linux-armhf.so.3'." >&2
  echo "Remove them, or re-sync with: tools/cross/sync_sysroot.sh --host $HOST" >&2
  exit 1
fi

if [[ "$BUILD_IMAGE" == "auto" ]]; then
  if docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    echo "Reusing existing Docker image: $IMAGE_TAG"
    BUILD_IMAGE="false"
  else
    echo "Docker image is missing; building $IMAGE_TAG"
    BUILD_IMAGE="true"
  fi
fi

if [[ "$BUILD_IMAGE" == "true" ]]; then
  # Context is the docker directory alone. The Dockerfile COPYs nothing, and a
  # repo-root context would tar up the sysroot and every build tree.
  DOCKER_BUILD_ARGS=(--progress=plain -t "$IMAGE_TAG" -f "$CROSS_DIR/docker/Dockerfile")
  if [[ "$CLEAN" == "true" ]]; then
    DOCKER_BUILD_ARGS=(--no-cache "${DOCKER_BUILD_ARGS[@]}")
  fi
  docker build "${DOCKER_BUILD_ARGS[@]}" "$CROSS_DIR/docker"
fi

CONTAINER_SCRIPT="$(mktemp)"
cat > "$CONTAINER_SCRIPT" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/repo/$PROJECT"
SYSROOT_DIR="/repo/tools/cross/sysroot"
VERIFY_DIR="/repo/tools/cross/verify/$PROJECT/$DEPLOYMENT"
BUILD_DIR="$PROJECT_DIR/build-fprime-automatic-$TOOLCHAIN"

# Python environments live under tools/cross/venv and are shared between
# projects rather than rebuilt per project. They are keyed by the contents of
# the project's requirements.txt, so any two projects pinning the same F Prime
# release reuse one environment, while projects on different releases stay
# isolated. Without that isolation a project could be autocoded by another
# project's fprime-fpp, which is a silent, hard-to-trace failure.
REQUIREMENTS="$PROJECT_DIR/lib/fprime/requirements.txt"
REQ_KEY="$(sha256sum "$REQUIREMENTS" | cut -c1-12)"
BUILD_VENV="/repo/tools/cross/venv/$REQ_KEY"

if [[ "${CLEAN_VENV:-false}" == "true" ]]; then
  rm -rf "$BUILD_VENV"
fi

if [[ -x "$BUILD_VENV/bin/python" ]]; then
  echo "Reusing cross Python environment: $BUILD_VENV (requirements $REQ_KEY)"
  NEW_VENV="false"
else
  echo "Creating cross Python environment: $BUILD_VENV (requirements $REQ_KEY)"
  mkdir -p "$(dirname "$BUILD_VENV")"
  python3 -m venv "$BUILD_VENV"
  NEW_VENV="true"
fi

# shellcheck disable=SC1090
. "$BUILD_VENV/bin/activate"

if [[ "$NEW_VENV" == "true" ]] || ! fprime-util --help >/dev/null 2>&1; then
  python -m pip install --upgrade pip
  python -m pip install -r "$REQUIREMENTS"
fi

export ARM_TOOLS_PATH=/usr

# A build cache records absolute paths into the venv that generated it (ninja,
# cmake, python). If that venv has moved or been rebuilt under a new
# requirements hash, the cache is stale and CMake fails with a bare
# "No such file or directory". Detect it and regenerate instead.
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  STALE_REASON=""

  CACHED_MAKE="$(sed -n 's/^CMAKE_MAKE_PROGRAM:FILEPATH=//p' "$BUILD_DIR/CMakeCache.txt" | head -n 1)"
  if [[ -n "$CACHED_MAKE" && ! -x "$CACHED_MAKE" ]]; then
    STALE_REASON="its build tool no longer exists ($CACHED_MAKE)"
  fi

  # A configure that failed before project() completed leaves a cache with no
  # CMAKE_PROJECT_NAME. F Prime reports that as "could not find
  # CMAKE_PROJECT_NAME in Cache", which says nothing about the real cause.
  if [[ -z "$STALE_REASON" ]] && ! grep -q '^CMAKE_PROJECT_NAME:' "$BUILD_DIR/CMakeCache.txt"; then
    STALE_REASON="a previous configure failed partway and left it incomplete"
  fi

  if [[ -n "$STALE_REASON" ]]; then
    echo "Discarding unusable build cache: $STALE_REASON"
    echo "Regenerating $BUILD_DIR"
    rm -rf "$BUILD_DIR"
  fi
fi

cd "$PROJECT_DIR"
if [[ "${CLEAN_BUILD:-false}" == "true" || ! -d "$BUILD_DIR" ]]; then
  GENERATE_ARGS=("$TOOLCHAIN")
  if [[ "${CLEAN_BUILD:-false}" == "true" ]]; then
    GENERATE_ARGS+=("-f")
  fi
  GENERATE_ARGS+=(
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_SYSROOT=$SYSROOT_DIR"
    "-DARTEMIS_ARM_HELPERS_DIR=$PROJECT_DIR/lib/fprime/cmake/toolchain/helpers"
    "-DCMAKE_VERBOSE_MAKEFILE=ON"
  )
  fprime-util generate "${GENERATE_ARGS[@]}"
else
  echo "Reusing existing F Prime build cache: $BUILD_DIR"
fi

# Build from inside the deployment directory so only the selected deployment
# and its dependencies are built, whatever the project layout is.
cd "$PROJECT_DIR/$DEPLOYMENT_REL"
fprime-util build "$TOOLCHAIN"

# Artifact directories are named after the deployment, but nested projects
# prefix them with the module path (FprimeArtemisCore_Deployments_<name>).
ARTIFACT_ROOT="$PROJECT_DIR/build-artifacts/$TOOLCHAIN"
ARTIFACT_DIR="$(find "$ARTIFACT_ROOT" -mindepth 1 -maxdepth 1 -type d \
  \( -name "$DEPLOYMENT" -o -name "*_$DEPLOYMENT" \) 2>/dev/null | head -n 1)"

if [[ -z "$ARTIFACT_DIR" ]]; then
  echo "No artifact directory for '$DEPLOYMENT' under $ARTIFACT_ROOT" >&2
  ls -1 "$ARTIFACT_ROOT" 2>/dev/null >&2 || true
  exit 1
fi

BIN_PATH="$(find "$ARTIFACT_DIR/bin" -maxdepth 1 -type f -perm /111 2>/dev/null | head -n 1)"
DICT_PATH="$(find "$ARTIFACT_DIR/dict" -maxdepth 1 -type f -name '*TopologyDictionary.json' 2>/dev/null | head -n 1)"

if [[ -z "$BIN_PATH" ]]; then
  echo "Failed to locate a built binary under $ARTIFACT_DIR/bin" >&2
  exit 1
fi

mkdir -p "$VERIFY_DIR"
file "$BIN_PATH" | tee "$VERIFY_DIR/file.txt"
readelf -A "$BIN_PATH" | tee "$VERIFY_DIR/readelf-A.txt"
readelf -l "$BIN_PATH" | tee "$VERIFY_DIR/readelf-l.txt"

if ! grep -Eq 'Tag_CPU_arch: v6($|KZ)' "$VERIFY_DIR/readelf-A.txt"; then
  echo "Cross-build produced a non-Pi-Zero-W ISA; expected ARMv6-compatible attributes" >&2
  exit 1
fi

printf '%s\n' "$BIN_PATH" > "$VERIFY_DIR/binary-path.txt"
printf '%s\n' "$DICT_PATH" > "$VERIFY_DIR/dictionary-path.txt"
EOF
chmod +x "$CONTAINER_SCRIPT"

docker run --rm \
  -e CLEAN_BUILD="$CLEAN" \
  -e CLEAN_VENV="$CLEAN" \
  -e PROJECT="$PROJECT" \
  -e DEPLOYMENT="$DEPLOYMENT" \
  -e DEPLOYMENT_REL="$DEPLOYMENT_REL" \
  -e TOOLCHAIN="$TOOLCHAIN" \
  -v "$REPO_ROOT:/repo" \
  -v "$CONTAINER_SCRIPT:/tmp/run-build.sh:ro" \
  -w "/repo/$PROJECT" \
  "$IMAGE_TAG" \
  /tmp/run-build.sh

rm -f "$CONTAINER_SCRIPT"

fi

if [[ ! -f "$VERIFY_DIR/binary-path.txt" ]]; then
  echo "No previously verified binary recorded at $VERIFY_DIR/binary-path.txt" >&2
  echo "Run a build first before using --copy-only." >&2
  exit 1
fi

BIN_PATH="$(cat "$VERIFY_DIR/binary-path.txt")"
BIN_PATH="${BIN_PATH/#\/repo/$REPO_ROOT}"

if [[ ! -f "$BIN_PATH" ]]; then
  echo "Recorded binary no longer exists: $BIN_PATH" >&2
  echo "Run a build first before using --copy-only." >&2
  exit 1
fi

if [[ ! -f "$VERIFY_DIR/file.txt" || ! -f "$VERIFY_DIR/readelf-A.txt" || ! -f "$VERIFY_DIR/readelf-l.txt" ]]; then
  echo "Recorded verification files are incomplete in $VERIFY_DIR" >&2
  echo "Run a build first before using --copy-only." >&2
  exit 1
fi

echo "Local binary verification"
cat "$VERIFY_DIR/file.txt"
echo
echo "Interpreter"
grep -n "Requesting program interpreter" "$VERIFY_DIR/readelf-l.txt" || true
echo
echo "ARM attributes"
sed -n '1,120p' "$VERIFY_DIR/readelf-A.txt"

if [[ "$LOCAL_ONLY" == "true" ]]; then
  echo
  echo "Local-only cross compile + verification completed successfully"
  echo "  project: $PROJECT"
  echo "  deployment: $DEPLOYMENT"
  echo "  binary: $BIN_PATH"
  echo "  verify dir: $VERIFY_DIR"
  exit 0
fi

REMOTE_BIN="$REMOTE_DIR/$(basename "$BIN_PATH")"

ssh "$HOST" "mkdir -p '$REMOTE_DIR'"
scp "$BIN_PATH" "$HOST:$REMOTE_BIN"

if [[ "$SKIP_SMOKE" == "true" ]]; then
  ssh "$HOST" "
    set -euo pipefail
    chmod +x '$REMOTE_BIN'
    echo 'remote file:'
    file '$REMOTE_BIN'
    echo
    echo 'remote ldd:'
    ldd '$REMOTE_BIN'
    echo
    echo 'smoke test: skipped (--skip-smoke)'
  "
else
  ssh "$HOST" "
    set -euo pipefail
    chmod +x '$REMOTE_BIN'
    echo 'remote file:'
    file '$REMOTE_BIN'
    echo
    echo 'remote ldd:'
    ldd '$REMOTE_BIN'
    echo
    echo 'smoke test:'
    set +e
    timeout 8s '$REMOTE_BIN' -d /dev/null > '$REMOTE_DIR/smoke.log' 2>&1
    status=\$?
    set -e
    cat '$REMOTE_DIR/smoke.log'
    if [[ \$status -ne 0 && \$status -ne 124 ]]; then
      echo
      echo \"Smoke test failed with exit status \$status\" >&2
      exit \$status
    fi
  "
fi

echo
echo "Smoke test completed successfully on $HOST"
echo "  project: $PROJECT"
echo "  deployment: $DEPLOYMENT"
echo "  binary: $BIN_PATH"
echo "  remote: $REMOTE_BIN"
