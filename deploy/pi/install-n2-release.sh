#!/bin/bash
# Install a staged, paired N2 CDR release and headless Pi Zero W configuration.
set -euo pipefail
[[ $(id -u) = 0 ]] || { echo 'Run with sudo on n2-obc.' >&2; exit 1; }
STAGE=$(realpath "${1:?staging directory required}")
BASE=/home/n2-obc/artemis
RELEASE=d4fe283-cdr-tcp-20261026
BACKUP="$BASE/setup-backup-$(date -u +%Y%m%dT%H%M%SZ)-n2-update"
export STAGE BASE BACKUP RELEASE
[[ $(hostname) = n2-obc && $(uname -m) = armv6l ]]
if [[ -e "$BASE/releases/$RELEASE" || -L "$BASE/releases/$RELEASE" ]]; then
  echo "Refusing to overwrite existing release $BASE/releases/$RELEASE; select a new release name." >&2
  exit 1
fi
[[ -c /dev/serial0 ]]
if findmnt -rn -t nfs,nfs4 > /dev/null; then
  echo "Refusing boot-service changes while NFS mounts are active." >&2
  exit 1
fi
cd "$STAGE"
sha256sum -c SHA256SUMS
ldd ./ArtemisRpiTeensyDeployment | tee /tmp/n2-release-ldd.txt
if grep -q 'not found' /tmp/n2-release-ldd.txt; then
  echo 'Release has unresolved shared libraries.' >&2
  exit 1
fi
python3 -m py_compile artemis-supervisor.py
mkdir -p "$BASE/releases"
mkdir "$BACKUP" "$BASE/releases/$RELEASE"
chmod 700 "$BACKUP"
systemctl list-unit-files --no-pager > "$BACKUP/unit-states.txt"
systemd-analyze time > "$BACKUP/boot-before.txt"
readlink "$BASE/current" > "$BACKUP/previous-release.txt"
# Save every file/symlink touched by the configuration changes, including absence.
python3 - <<'PY'
import json, os, shutil
from pathlib import Path
base=Path(os.environ['BASE']); backup=Path(os.environ['BACKUP'])
system_units=['rpcbind.service','rpcbind.socket','nfs-blkmap.service','nfs-client.target',
              'nfs-utils.service','glamor-test.service','rp1-test.service','rpi-eeprom-update.service']
user_units=['pipewire.service','pipewire.socket','pipewire-pulse.service','pipewire-pulse.socket',
            'wireplumber.service','filter-chain.service','mpris-proxy.service']
paths=[Path('/etc/systemd/system/artemis-fprime.service'),base/'artemis-supervisor.py',
       Path('/etc/rpi/swap.conf.d/80-artemis-zram.conf'),Path('/boot/firmware/config.txt'),
       Path('/etc/systemd/journald.conf.d/80-artemis.conf')]
paths += [Path('/etc/systemd/system')/x for x in system_units]
paths += [Path('/home/n2-obc/.config/systemd/user')/x for x in user_units]
items=[]
for p in paths:
    present=p.exists() or p.is_symlink()
    items.append({'path':str(p),'present':present})
    if present:
        dst=backup/'files'/str(p).lstrip('/');dst.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(p,dst,follow_symlinks=False)
(backup/'files.json').write_text(json.dumps(items,indent=2)+'\n')
(backup/'system-masks.txt').write_text('\n'.join(system_units)+'\n')
(backup/'user-masks.txt').write_text('\n'.join(user_units)+'\n')
PY
cat > "$BACKUP/rollback.sh" <<'ROLLBACK'
#!/bin/bash
set -euo pipefail
[[ $(id -u) = 0 ]]
BACKUP=$(cd -- "$(dirname -- "$0")" && pwd)
export BACKUP
systemctl stop artemis-fprime.service
python3 - <<'PY'
from pathlib import Path
import json,os,shutil
b=Path(os.environ['BACKUP'])
for item in json.loads((b/'files.json').read_text()):
    p=Path(item['path'])
    if p.exists() or p.is_symlink(): p.unlink()
    if item['present']:
        p.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(b/'files'/str(p).lstrip('/'),p,follow_symlinks=False)
base=Path('/home/n2-obc/artemis')
target=(b/'previous-release.txt').read_text().strip()
p=base/'.current-rollback';p.symlink_to(target);p.replace(base/'current')
PY
systemctl daemon-reload
systemctl start artemis-fprime.service
echo 'Previous release/config restored. Reboot to restore boot/swap settings.'
ROLLBACK
chmod 700 "$BACKUP/rollback.sh"
# Rollback is available before changing the installed release or configuration.
for directory in "$BASE/state" "$BASE/data/science-products"; do
  if [[ ! -d "$directory" ]]; then
    install -d -o n2-obc -g n2-obc -m 750 "$directory"
  fi
done
install -m 755 ArtemisRpiTeensyDeployment "$BASE/releases/$RELEASE/"
install -m 644 ArtemisRpiTeensyDeploymentTopologyDictionary.json build-manifest.json "$BASE/releases/$RELEASE/"
install -m 644 artemis-supervisor.py "$BASE/artemis-supervisor.py"
install -m 644 artemis-fprime.service /etc/systemd/system/artemis-fprime.service
systemd-analyze verify /etc/systemd/system/artemis-fprime.service
mkdir -p /etc/rpi/swap.conf.d /etc/systemd/journald.conf.d
printf '[Main]\nMechanism=zram\n' > /etc/rpi/swap.conf.d/80-artemis-zram.conf
printf '[Journal]\nStorage=persistent\nSystemMaxUse=32M\nRuntimeMaxUse=8M\n' > /etc/systemd/journald.conf.d/80-artemis.conf
python3 - <<'PY'
from pathlib import Path
p=Path('/boot/firmware/config.txt');s=p.read_text()
for old,new in [('dtparam=audio=on','dtparam=audio=off'),
                ('camera_auto_detect=1','camera_auto_detect=0'),
                ('display_auto_detect=1','display_auto_detect=0')]:
    s=s.replace(old,new)
p.write_text(s)
PY
while read -r unit; do systemctl --no-reload mask "$unit"; done < "$BACKUP/system-masks.txt"
USER_UNITS=/home/n2-obc/.config/systemd/user
mkdir -p "$USER_UNITS"
while read -r unit; do ln -sfn /dev/null "$USER_UNITS/$unit"; done < "$BACKUP/user-masks.txt"
chown n2-obc:n2-obc /home/n2-obc/.config /home/n2-obc/.config/systemd "$USER_UNITS"

# Keep state and science data in place while switching only the release symlink.
systemctl stop artemis-fprime.service
ln -s "releases/$RELEASE" "$BASE/.current-update"
mv -Tf "$BASE/.current-update" "$BASE/current"
systemctl daemon-reload
systemctl enable artemis-fprime.service
systemctl reset-failed artemis-fprime.service
systemctl start --no-block artemis-fprime.service
sync
printf 'Installed %s. Backup/rollback: %s\nReboot is required for swap and headless configuration.\n' "$RELEASE" "$BACKUP"
