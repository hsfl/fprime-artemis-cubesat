#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${1:-v2.0.0-epscorc3m-demo}"
OUT_DIR="${2:-$ROOT/release-artifacts}"
ASSET_NAME="epscorc3m-v2.0.0-demo-artifacts"
ZIP="$OUT_DIR/$ASSET_NAME.zip"

EXPECTED_BIN_SHA="ed443c037020228d6959d7261b99f97d93c12013b7ad787a49f2fe86f20062da"
EXPECTED_DICT_SHA="1e8f234c16fd8af0fbe9f1fd4f08fba8aafeb5bdceb8141e9dac0df3906b0f30"
EXPECTED_GROUND_SHA="010ebce0eb3eab1c1403c29fc17633b8d5e6094a962deaa318a9eb91342b5d78"
EXPECTED_SAT_SHA="75f85af0df9695628848ff7dbb067b2621e5196cb15f69625f73d55bebee804e"

BIN="$ROOT/ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment"
DICT="$ROOT/ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json"
GROUND_HEX="$ROOT/GDS_Teensy/build/arduino-cli/gds_teensy.ino.hex"
GROUND_ELF="$ROOT/GDS_Teensy/build/arduino-cli/gds_teensy.ino.elf"
SAT_HEX="$ROOT/ArtemisTeensy_N2_Baremetal/build/arduino-cli/satellite_teensy.ino.hex"
SAT_ELF="$ROOT/ArtemisTeensy_N2_Baremetal/build/arduino-cli/satellite_teensy.ino.elf"

require_hash() {
  local path="$1" expected="$2" actual
  [[ -f "$path" ]] || { echo "Missing artifact: $path" >&2; exit 1; }
  actual="$(shasum -a 256 "$path" | awk '{print $1}')"
  [[ "$actual" == "$expected" ]] || {
    echo "Refusing unvalidated artifact: $path" >&2
    echo "expected: $expected" >&2
    echo "actual:   $actual" >&2
    exit 1
  }
}

cd "$ROOT"
COMMIT="$(git rev-parse --verify "$TAG^{commit}")"
[[ "$(git rev-parse HEAD)" == "$COMMIT" ]] || {
  echo "Check out tagged commit $TAG before packaging." >&2
  exit 1
}
[[ -z "$(git status --porcelain --untracked-files=no)" ]] || {
  echo "Tracked worktree changes present; refusing release package." >&2
  exit 1
}

require_hash "$BIN" "$EXPECTED_BIN_SHA"
require_hash "$DICT" "$EXPECTED_DICT_SHA"
require_hash "$GROUND_HEX" "$EXPECTED_GROUND_SHA"
require_hash "$SAT_HEX" "$EXPECTED_SAT_SHA"
[[ -f "$GROUND_ELF" && -f "$SAT_ELF" ]] || { echo "Missing Teensy ELF artifact." >&2; exit 1; }
file "$BIN" | grep -q 'ELF 32-bit.*ARM'

TMP="$(mktemp -d "${TMPDIR:-/tmp}/c3m-release.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
SOURCE="$TMP/source-tree"
BUNDLE="$TMP/$ASSET_NAME"
mkdir -p "$SOURCE" "$BUNDLE/artifacts/rpi" \
  "$BUNDLE/artifacts/teensy-ground" "$BUNDLE/artifacts/teensy-satellite" \
  "$BUNDLE/scripts" "$BUNDLE/source"

git archive --format=tar "$TAG" | tar -xf - -C "$SOURCE"
git archive --format=tar.gz --prefix="$TAG-source/" "$TAG" \
  > "$BUNDLE/source/$TAG-source.tar.gz"

cp "$SOURCE/release/epscorc3m/README.md" "$BUNDLE/README.md"
cp "$SOURCE/release/epscorc3m/RELEASE_NOTES_V2.md" "$BUNDLE/RELEASE_NOTES.md"
cp "$SOURCE/release/epscorc3m/requirements-ground.txt" "$BUNDLE/"
cp "$SOURCE/release/epscorc3m/flash_ground_teensy.sh" "$BUNDLE/scripts/"
cp "$SOURCE/release/epscorc3m/flash_satellite_teensy.sh" "$BUNDLE/scripts/"
cp "$SOURCE/release/epscorc3m/deploy_rpi_release.sh" "$BUNDLE/scripts/"
chmod 0755 "$BUNDLE/scripts/"*.sh

cp "$BIN" "$BUNDLE/artifacts/rpi/"
cp "$DICT" "$BUNDLE/artifacts/rpi/"
cp "$SOURCE/deploy/pi/artemis-fprime.service" "$BUNDLE/artifacts/rpi/"
cp "$GROUND_HEX" "$GROUND_ELF" "$BUNDLE/artifacts/teensy-ground/"
cp "$SAT_HEX" "$SAT_ELF" "$BUNDLE/artifacts/teensy-satellite/"

mkdir -p "$BUNDLE/ArtemisRpiTeensy_N2/tools" "$BUNDLE/ground-station" "$BUNDLE/docs"
cp "$SOURCE/ArtemisRpiTeensy_N2/tools/payload_receiver.py" "$BUNDLE/ArtemisRpiTeensy_N2/tools/"
cp "$SOURCE/ArtemisRpiTeensy_N2/tools/run_gds_uart.sh" "$BUNDLE/ArtemisRpiTeensy_N2/tools/"
cp -R "$SOURCE/ground-station/c3m-payload-receiver-ui" "$BUNDLE/ground-station/"
cp -R "$SOURCE/ground-station/lepton-dp-viewer" "$BUNDLE/ground-station/"
cp -R "$SOURCE/ground-station/c3m-lepton-test-data" "$BUNDLE/ground-station/"
cp -R "$SOURCE/ground-station/boson-viewer" "$BUNDLE/ground-station/"
cp -R "$SOURCE/ground-station/hackrf-rf22" "$BUNDLE/ground-station/"
cp "$SOURCE/tools/c3m" "$BUNDLE/scripts/"
cp "$SOURCE/tools/c3m-sdr" "$BUNDLE/scripts/"
chmod 0755 "$BUNDLE/scripts/c3m" "$BUNDLE/scripts/c3m-sdr"
for doc in \
  C3M_DEMO_HARDENING_PLAN_2026-07-09.md \
  C3M_LEPTON_RF_HIL_SCRATCHPAD_2026-07-09.md \
  C3M_PAYLOAD_RECEIVER_WEB_UI_PLAN.md \
  EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md \
  HIL_BENCH_HANDOFF_2026-07-09.md \
  C3M_MVP_DOWNLINK_PROTOCOL.md \
  C3M_ON_DEMAND_TEENSY_CACHE_DOWNLINK_PLAN.md \
  C3M_LEPTON_PREVIEW_STREAM_MVP.md \
  C3M_LIVE_QA_LOG_2026-08-03.md \
  NEUTRON2_RADIO_ARCHITECTURE_SUMMARY.md \
  archive/C3M_HACKRF_GROUND_STATION_DECISION_2026-08-06.md; do
  mkdir -p "$BUNDLE/docs/$(dirname "$doc")"
  cp "$SOURCE/docs/$doc" "$BUNDLE/docs/$doc"
done

{
  echo "release_name=$TAG"
  echo "git_commit=$COMMIT"
  echo "created_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "pi_target=Raspberry Pi Zero W ARMv6 hard-float"
  echo "pi_interpreter=/lib/ld-linux-armhf.so.3"
  echo "camera_backend=uvc"
  echo "ground_fqbn=teensy:avr:teensy41:usb=serial3"
  echo "satellite_fqbn=teensy:avr:teensy41"
  echo
  echo "submodules:"
  git ls-tree -r "$TAG" | awk '$1 == "160000" {print $3 "  " $4}'
} > "$BUNDLE/RELEASE_INFO.txt"

(
  cd "$BUNDLE"
  find . -type f ! -name SHA256SUMS -print0 | sort -z | \
    xargs -0 shasum -a 256 > SHA256SUMS
  shasum -a 256 -c SHA256SUMS >/dev/null
)

mkdir -p "$OUT_DIR"
rm -f "$ZIP"
(
  cd "$TMP"
  zip -X -q -r "$ZIP" "$ASSET_NAME"
)
unzip -tq "$ZIP"
shasum -a 256 "$ZIP"
echo "Created: $ZIP"
