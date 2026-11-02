#!/bin/bash
# Reversible desktop-service cleanup for the headless n2-obc Pi Zero W.
set -euo pipefail
[[ $(id -u) = 0 && $(hostname) = n2-obc && $(uname -m) = armv6l ]]
BASE=/home/n2-obc/artemis
BACKUP="$BASE/desktop-backup-$(date -u +%Y%m%dT%H%M%SZ)"
export BACKUP
mkdir -m 700 "$BACKUP"
systemctl list-unit-files --no-pager > "$BACKUP/unit-states.txt"
python3 - <<'PY'
from pathlib import Path
import os,json,shutil
b=Path(os.environ['BACKUP']);items=[]
system='''accounts-daemon.service udisks2.service upower.service colord.service
packagekit.service packagekit-offline-update.service power-profiles-daemon.service
ModemManager.service lightdm.service wayvnc.service wayvnc-control.service
vncserver-x11-serviced.service vncserver-virtuald.service cups.service cups.socket
cups.path cups-browsed.service bluetooth.service hciuart.service'''.split()
user='''fbd-alert-slider.service gcr-ssh-agent.service gcr-ssh-agent.socket
gnome-keyring-daemon.service gnome-keyring-daemon.socket
xdg-desktop-portal.service xdg-desktop-portal-gtk.service
xdg-desktop-portal-wlr.service xdg-desktop-portal-rewrite-launchers.service
xdg-document-portal.service xdg-permission-store.service
gvfs-afc-volume-monitor.service gvfs-daemon.service gvfs-goa-volume-monitor.service
gvfs-gphoto2-volume-monitor.service gvfs-metadata.service gvfs-mtp-volume-monitor.service
gvfs-udisks2-volume-monitor.service rpi-connect-wayvnc.service'''.split()
selected=[]
for scope,names in [('system',system),('user',user)]:
 for name in names:
  if not (Path('/usr/lib/systemd')/scope/name).exists(): continue
  directory=Path('/etc/systemd/system') if scope=='system' else Path('/home/n2-obc/.config/systemd/user')
  p=directory/name;present=p.exists() or p.is_symlink()
  items.append({'path':str(p),'present':present})
  if present:
   dst=b/'files'/str(p).lstrip('/');dst.parent.mkdir(parents=True,exist_ok=True)
   shutil.copy2(p,dst,follow_symlinks=False)
  selected.append(scope+' '+name)
(b/'files.json').write_text(json.dumps(items,indent=2)+'\n')
(b/'masked-units.txt').write_text('\n'.join(selected)+'\n')
print('\n'.join(selected))
PY
cat > "$BACKUP/rollback.sh" <<'ROLLBACK'
#!/bin/bash
set -euo pipefail
[[ $(id -u) = 0 ]]
export BACKUP=$(cd -- "$(dirname -- "$0")" && pwd)
python3 - <<'PY'
from pathlib import Path
import os,json,shutil
b=Path(os.environ['BACKUP'])
for item in json.loads((b/'files.json').read_text()):
 p=Path(item['path'])
 if p.exists() or p.is_symlink(): p.unlink()
 if item['present']: shutil.copy2(b/'files'/str(p).lstrip('/'),p,follow_symlinks=False)
PY
systemctl daemon-reload
echo 'Desktop masks restored to their prior state; reboot to apply user-service state.'
ROLLBACK
chmod 700 "$BACKUP/rollback.sh"
# Snapshot and executable rollback are complete before applying any mask.
python3 - <<'APPLY'
from pathlib import Path
import os,json
b=Path(os.environ['BACKUP'])
for item in json.loads((b/'files.json').read_text()):
 p=Path(item['path'])
 p.parent.mkdir(parents=True,exist_ok=True)
 if p.exists() or p.is_symlink(): p.unlink()
 p.symlink_to('/dev/null')
APPLY
chown n2-obc:n2-obc /home/n2-obc/.config/systemd /home/n2-obc/.config/systemd/user
systemctl daemon-reload
echo "Desktop services masked; packages retained. Rollback: $BACKUP/rollback.sh"
echo 'Reboot to verify the final headless configuration.'
