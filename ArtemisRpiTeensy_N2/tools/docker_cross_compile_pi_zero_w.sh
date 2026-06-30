#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
IMAGE_TAG="artemis-pi-zero-w-cross:local"
HOST="${PI_ZERO_W_SSH_HOST:-pi@raspberrypi-zero-w}"
SYSROOT_DIR="${PI_ZERO_W_SYSROOT_DIR:-$ROOT_DIR/cross/pi-zero-w/sysroot}"
VERIFY_DIR="$ROOT_DIR/cross/pi-zero-w/verify"
DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"
REMOTE_DIR="${PI_ZERO_W_REMOTE_DIR:-/home/pi/artemis/cross}"
SYNC_SYSROOT="true"
BUILD_IMAGE="true"
LOCAL_ONLY="false"
COPY_ONLY="false"

usage() {
  cat <<'EOF'
Usage: docker_cross_compile_pi_zero_w.sh [options]

Build the Artemis RPi deployment for Pi Zero W inside Docker, verify the
binary attributes locally, upload it to the Pi, and run the /dev/null smoke
test remotely.

Update these for your own setup before the first run:
  1. SSH target: either export PI_ZERO_W_SSH_HOST or pass --host
  2. Remote deploy directory: either export PI_ZERO_W_REMOTE_DIR or pass --remote-dir
  3. Optional local sysroot location: export PI_ZERO_W_SYSROOT_DIR or pass --sysroot

Examples:
  export PI_ZERO_W_SSH_HOST=pi@192.168.1.44
  export PI_ZERO_W_REMOTE_DIR=/home/pi/artemis/cross
  ./tools/docker_cross_compile_pi_zero_w.sh

  ./tools/docker_cross_compile_pi_zero_w.sh --host my-pi-alias --remote-dir /tmp/artemis-cross

Options:
  --host <ssh-host>     SSH host alias or user@host
  --sysroot <path>      Sysroot directory (default: cross/pi-zero-w/sysroot)
  --remote-dir <path>   Remote deploy directory on the Pi
  --skip-sync           Reuse an existing sysroot without rsync
  --skip-image-build    Reuse the existing Docker image tag
  --local-only          Build + verify locally only (skip SSH deploy/smoke)
  --copy-only           Skip sync/build; deploy the previously built binary and
                        run the remote smoke test only (requires a prior build)
  -h, --help            Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
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
    --skip-sync)
      SYNC_SYSROOT="false"
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

mkdir -p "$VERIFY_DIR"

echo "Cross-build target configuration"
echo "  ssh host: $HOST"
echo "  sysroot: $SYSROOT_DIR"
echo "  remote dir: $REMOTE_DIR"
echo "  docker image: $IMAGE_TAG"
echo "  local only: $LOCAL_ONLY"
echo "  copy only: $COPY_ONLY"
echo

# --copy-only skips the entire sync/build pipeline below and reuses the binary
# recorded by a previous run; the build body is left un-indented because the
# container heredoc's closing 'EOF' must stay at column 0.
if [[ "$COPY_ONLY" != "true" ]]; then

if [[ "$LOCAL_ONLY" == "true" && "$SYNC_SYSROOT" == "true" ]]; then
  echo "Local-only mode selected; skipping sysroot sync (reusing existing sysroot)"
  SYNC_SYSROOT="false"
fi

if [[ "$SYNC_SYSROOT" == "true" ]]; then
  "$ROOT_DIR/tools/sync_pi_zero_w_sysroot.sh" --host "$HOST" --dest "$SYSROOT_DIR"
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

if [[ "$BUILD_IMAGE" == "true" ]]; then
  docker build --progress=plain -t "$IMAGE_TAG" -f "$ROOT_DIR/cross/pi-zero-w/docker/Dockerfile" "$ROOT_DIR"
fi

CONTAINER_SCRIPT="$(mktemp)"
cat > "$CONTAINER_SCRIPT" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="/repo/ArtemisRpiTeensy_N2"
SYSROOT_DIR="$ROOT_DIR/cross/pi-zero-w/sysroot"
VERIFY_DIR="$ROOT_DIR/cross/pi-zero-w/verify"
TOOLCHAIN="pi-zero-w-armv6hf"
BUILD_VENV="$ROOT_DIR/.cross-venv-linux"
DEPLOYMENT_NAME="ArtemisRpiTeensyDeployment"

python3 -m venv --clear "$BUILD_VENV"
# shellcheck disable=SC1090
. "$BUILD_VENV/bin/activate"
python -m pip install --upgrade pip
python -m pip install -r "$ROOT_DIR/lib/fprime/requirements.txt"

export ARM_TOOLS_PATH=/usr

cd "$ROOT_DIR"
fprime-util generate "$TOOLCHAIN" -f \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSROOT="$SYSROOT_DIR" \
  -DCMAKE_VERBOSE_MAKEFILE=ON
fprime-util build "$TOOLCHAIN"

BIN_PATH="$(find "$ROOT_DIR/build-artifacts" -type f -path "*/${DEPLOYMENT_NAME}/bin/${DEPLOYMENT_NAME}" | grep "/${TOOLCHAIN}/" | head -n 1 || true)"
DICT_PATH="$(find "$ROOT_DIR/build-artifacts" -type f -path "*/${DEPLOYMENT_NAME}/dict/${DEPLOYMENT_NAME}TopologyDictionary.json" | grep "/${TOOLCHAIN}/" | head -n 1 || true)"

if [[ -z "$BIN_PATH" ]]; then
  echo "Failed to locate built deployment binary for ${TOOLCHAIN}" >&2
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
  -v "$REPO_ROOT:/repo" \
  -v "$CONTAINER_SCRIPT:/tmp/run-build.sh:ro" \
  -w /repo/ArtemisRpiTeensy_N2 \
  "$IMAGE_TAG" \
  /tmp/run-build.sh

rm -f "$CONTAINER_SCRIPT"

fi  # end sync/build pipeline (skipped when --copy-only)

if [[ ! -f "$VERIFY_DIR/binary-path.txt" ]]; then
  echo "No previously built binary recorded at $VERIFY_DIR/binary-path.txt" >&2
  echo "Run a full build first (without --copy-only) before using --copy-only." >&2
  exit 1
fi

BIN_PATH="$(cat "$VERIFY_DIR/binary-path.txt")"
BIN_PATH="${BIN_PATH/#\/repo\/ArtemisRpiTeensy_N2/$ROOT_DIR}"

if [[ ! -f "$BIN_PATH" ]]; then
  echo "Recorded binary no longer exists: $BIN_PATH" >&2
  echo "Run a full build first (without --copy-only)." >&2
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
  echo "  binary: $BIN_PATH"
  echo "  verify dir: $VERIFY_DIR"
  exit 0
fi

ssh "$HOST" "mkdir -p '$REMOTE_DIR'"
scp "$BIN_PATH" "$HOST:$REMOTE_DIR/$DEPLOYMENT_NAME"

ssh "$HOST" "
  set -euo pipefail
  chmod +x '$REMOTE_DIR/$DEPLOYMENT_NAME'
  echo 'remote file:'
  file '$REMOTE_DIR/$DEPLOYMENT_NAME'
  echo
  echo 'remote ldd:'
  ldd '$REMOTE_DIR/$DEPLOYMENT_NAME'
  echo
  echo 'smoke test:'
  set +e
  timeout 8s '$REMOTE_DIR/$DEPLOYMENT_NAME' -d /dev/null > '$REMOTE_DIR/smoke.log' 2>&1
  status=\$?
  set -e
  cat '$REMOTE_DIR/smoke.log'
  if [[ \$status -ne 0 && \$status -ne 124 ]]; then
    echo
    echo \"Smoke test failed with exit status \$status\" >&2
    exit \$status
  fi
"

echo
echo "Smoke test completed successfully on $HOST"
echo "  binary: $BIN_PATH"
echo "  remote: $REMOTE_DIR/$DEPLOYMENT_NAME"
