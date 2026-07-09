#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${PI_ZERO_W_SSH_HOST:-pi@raspberrypi-zero-w}"
DEST="${PI_ZERO_W_SYSROOT_DIR:-$ROOT_DIR/cross/pi-zero-w/sysroot}"

usage() {
  cat <<'EOF'
Usage: sync_pi_zero_w_sysroot.sh [options]

Copy the minimum Raspberry Pi userspace needed for Pi Zero W cross-linking.

Before using this on a different machine, update the SSH target:
  export PI_ZERO_W_SSH_HOST=pi@192.168.1.44
  ./tools/sync_pi_zero_w_sysroot.sh

The SSH target must:
  - be reachable with key-based SSH or another non-interactive SSH setup
  - have rsync installed
  - allow `sudo rsync` on the Pi so system libraries can be copied

Options:
  --host <ssh-host>   SSH host alias or user@host
  --dest <path>       Destination sysroot directory
  -h, --help          Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host)
      HOST="${2:-}"
      shift 2
      ;;
    --dest)
      DEST="${2:-}"
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

mkdir -p "$DEST"

echo "Syncing Pi Zero W sysroot from $HOST"
echo "  destination: $DEST"
echo "  note: change --host or PI_ZERO_W_SSH_HOST for your own Pi"

rsync -aH --delete --delete-excluded \
  --rsync-path="sudo rsync" \
  --progress \
  --filter="+ /lib" \
  --filter="+ /lib/ld-linux-armhf.so.3" \
  --filter="+ /lib/arm-linux-gnueabihf" \
  --filter="+ /lib/arm-linux-gnueabihf/***" \
  --filter="+ /usr" \
  --filter="+ /usr/include" \
  --filter="+ /usr/include/***" \
  --filter="+ /usr/lib" \
  --filter="+ /usr/lib/gcc" \
  --filter="+ /usr/lib/gcc/arm-linux-gnueabihf" \
  --filter="+ /usr/lib/gcc/arm-linux-gnueabihf/***" \
  --filter="+ /usr/lib/arm-linux-gnueabihf" \
  --filter="+ /usr/lib/arm-linux-gnueabihf/***" \
  --filter="+ /usr/local" \
  --filter="+ /usr/local/include" \
  --filter="+ /usr/local/include/***" \
  --filter="+ /usr/local/lib" \
  --filter="+ /usr/local/lib/***" \
  --filter="+ /etc" \
  --filter="+ /etc/ld.so.conf" \
  --filter="+ /etc/ld.so.conf.d" \
  --filter="+ /etc/ld.so.conf.d/***" \
  --filter="- *" \
  "$HOST":/ "$DEST"/

# The Pi GCC directory supplies ARMv6-safe runtime/startup objects, but its
# compiler helper programs and LTO plugin are ARM executables. If they remain
# under a `-B<sysroot-gcc-dir>` search path, the x86_64 Docker cross-compiler
# may try to execute/load them and fail under qemu. Keep only link/runtime
# inputs here; the container provides its own host-side compiler helpers.
find "$DEST/usr/lib/gcc/arm-linux-gnueabihf" -type f \
  \( -name cc1 -o -name cc1plus -o -name collect2 -o -name lto1 \
     -o -name lto-wrapper -o -name g++-mapper-server \
     -o -name 'liblto_plugin.so*' \) \
  -delete

mkdir -p "$DEST/lib"
ln -sfn arm-linux-gnueabihf/ld-linux-armhf.so.3 "$DEST/lib/ld-linux-armhf.so.3"

ssh "$HOST" '
  echo "uname -a:"
  uname -a
  echo
  echo "dpkg --print-architecture:"
  dpkg --print-architecture
  echo
  echo "ld-linux:"
  readlink -f /lib/ld-linux-armhf.so.3
  echo
  echo "glibc:"
  ldd --version 2>&1 | head -n 1
' > "$DEST/metadata.txt"

echo "Saved target metadata to $DEST/metadata.txt"
