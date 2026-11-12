#!/usr/bin/env python3
"""Pause/resume only this container's real Yamcs Java process for acceptance."""
import argparse
import json
import os
from pathlib import Path
import signal

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=('pause', 'resume'))
args = parser.parse_args()
state = Path('/data/yamcs-paused-pids.json')
if args.action == 'pause':
    pids = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            command = (proc / 'cmdline').read_bytes().split(b'\0')
            if b'org.yamcs.YamcsServer' in command:
                pids.append(int(proc.name))
        except OSError:
            pass
    if len(pids) != 1:
        raise SystemExit(f'Expected exactly one actual Yamcs Java process: {pids}')
    state.write_text(json.dumps(pids))
    for pid in pids:
        os.kill(pid, signal.SIGSTOP)
else:
    pids = json.loads(state.read_text()) if state.exists() else []
    for pid in pids:
        try:
            os.kill(pid, signal.SIGCONT)
        except ProcessLookupError:
            pass
    state.unlink(missing_ok=True)
print(json.dumps({'action': args.action, 'pids': pids}))
