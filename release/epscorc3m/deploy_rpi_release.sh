#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${1:-artemis-pi-c3m}"
REMOTE_BASE="${C3M_REMOTE_BASE:-/home/pi/artemis}"
INFO="$ROOT/RELEASE_INFO.txt"
BIN="$ROOT/artifacts/rpi/ArtemisRpiTeensyDeployment"
DICT="$ROOT/artifacts/rpi/ArtemisRpiTeensyDeploymentTopologyDictionary.json"

for path in "$INFO" "$BIN" "$DICT"; do
  [[ -f "$path" ]] || { echo "Missing release file: $path" >&2; exit 1; }
done

RELEASE_NAME="$(awk -F= '$1 == "release_name" {print $2}' "$INFO")"
BIN_SHA="$(shasum -a 256 "$BIN" | awk '{print $1}')"
DICT_SHA="$(shasum -a 256 "$DICT" | awk '{print $1}')"
[[ -n "$RELEASE_NAME" ]] || { echo "Missing release_name in $INFO" >&2; exit 1; }

REMOTE_UPLOAD="/tmp/${RELEASE_NAME}-upload"
ssh "$HOST" "set -eu; rm -rf '$REMOTE_UPLOAD'; mkdir '$REMOTE_UPLOAD'"
scp -q "$BIN" "$HOST:$REMOTE_UPLOAD/ArtemisRpiTeensyDeployment"
scp -q "$DICT" "$HOST:$REMOTE_UPLOAD/ArtemisRpiTeensyDeploymentTopologyDictionary.json"

ssh "$HOST" bash -s -- \
  "$REMOTE_BASE" "$RELEASE_NAME" "$REMOTE_UPLOAD" "$BIN_SHA" "$DICT_SHA" <<'REMOTE'
set -euo pipefail
BASE="$1"
NAME="$2"
UPLOAD="$3"
BIN_SHA="$4"
DICT_SHA="$5"
RELEASES="$BASE/releases"
STAGE="$RELEASES/.stage-$NAME"
FINAL="$RELEASES/$NAME"
CURRENT="$BASE/current"
OLD="$(readlink -f "$CURRENT" 2>/dev/null || true)"
SWAPPED=0

rollback() {
  rc=$?
  if [[ "$SWAPPED" -eq 1 && -n "$OLD" ]]; then
    ln -sfn "$OLD" "$BASE/.current-rollback"
    mv -Tf "$BASE/.current-rollback" "$CURRENT"
    sudo -n systemctl restart artemis-fprime.service || true
  fi
  echo "Deployment failed; current release restored to: ${OLD:-none}" >&2
  exit "$rc"
}
trap rollback ERR

mkdir -p "$RELEASES"
[[ ! -e "$FINAL" ]] || { echo "Release already exists: $FINAL" >&2; exit 2; }
rm -rf "$STAGE"
mkdir "$STAGE"
mv "$UPLOAD/ArtemisRpiTeensyDeployment" "$STAGE/"
mv "$UPLOAD/ArtemisRpiTeensyDeploymentTopologyDictionary.json" "$STAGE/"
rmdir "$UPLOAD"

echo "$BIN_SHA  $STAGE/ArtemisRpiTeensyDeployment" | sha256sum -c -
echo "$DICT_SHA  $STAGE/ArtemisRpiTeensyDeploymentTopologyDictionary.json" | sha256sum -c -
chmod 0555 "$STAGE/ArtemisRpiTeensyDeployment"
chmod 0444 "$STAGE/ArtemisRpiTeensyDeploymentTopologyDictionary.json"
mkdir "$STAGE/DpCat"

LDD="$(ldd "$STAGE/ArtemisRpiTeensyDeployment")"
! grep -q 'not found' <<<"$LDD"
for library in libuvc libusb libudev libjpeg; do
  grep -q "$library" <<<"$LDD" || { echo "Missing required runtime library: $library" >&2; exit 1; }
done

mv "$STAGE" "$FINAL"
ln -sfn "$FINAL" "$BASE/.current-next"
mv -Tf "$BASE/.current-next" "$CURRENT"
SWAPPED=1
sudo -n systemctl restart artemis-fprime.service
sleep 8
systemctl is-active --quiet artemis-fprime.service
PID1="$(systemctl show artemis-fprime.service -p MainPID --value)"
RESTARTS1="$(systemctl show artemis-fprime.service -p NRestarts --value)"
[[ "$(readlink -f "/proc/$PID1/exe")" == "$FINAL/ArtemisRpiTeensyDeployment" ]]
tr '\0' '\n' < "/proc/$PID1/environ" | grep -qx 'LEPTON_CAMERA_BACKEND=uvc'
sleep 4
PID2="$(systemctl show artemis-fprime.service -p MainPID --value)"
RESTARTS2="$(systemctl show artemis-fprime.service -p NRestarts --value)"
[[ "$PID1" == "$PID2" && "$RESTARTS1" == "$RESTARTS2" ]]

trap - ERR
echo "Active release: $FINAL"
echo "PID: $PID2; NRestarts: $RESTARTS2"
REMOTE
