#!/usr/bin/env python3
"""Run a built ARMv6 Pi artifact under QEMU/PTY with recorded camera input only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import pty
import select
import subprocess
import tempfile
import time


def run(args):
    manifest = json.loads(args.manifest.read_text())
    binary = Path(manifest['binary_path'])
    if hashlib.sha256(binary.read_bytes()).hexdigest() != manifest['binary_sha256']:
        raise RuntimeError('Manifest does not identify the current Pi executable')
    fixture = args.fixture.resolve()
    if not fixture.is_file():
        raise RuntimeError('Explicit recorded Lepton fixture is required')
    master, slave = pty.openpty()
    output = bytearray()
    with tempfile.TemporaryDirectory(prefix='armv6-qemu-') as working:
        env = os.environ.copy()
        env.pop('ARTEMIS_TWIN', None)
        search_paths = manifest.get('library_search_paths', ['/usr/local/lib', '/lib/arm-linux-gnueabihf', '/usr/lib/arm-linux-gnueabihf'])
        env.update(LEPTON_CAMERA_BACKEND='sample', C3M_LEPTON_SAMPLE_CSV=str(fixture),
                   LD_LIBRARY_PATH=':'.join(str(args.sysroot / path.lstrip('/')) for path in search_paths))
        command = ['qemu-arm', '-cpu', 'arm1176', '-L', str(args.sysroot), str(binary),
                   '-d', os.ttyname(slave), '-b', '115200']
        process = subprocess.Popen(command, cwd=working, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                ready, _, _ = select.select([process.stdout, master], [], [], 0.1)
                for stream in ready:
                    if stream == master:
                        os.read(master, 4096)  # Drain the virtual FC UART; no physical device is opened.
                    else:
                        output.extend(os.read(process.stdout.fileno(), 65536))
                if process.poll() is not None:
                    raise RuntimeError(f'ARMv6 application exited early ({process.returncode}): {output.decode(errors="replace")}')
            process.terminate()
            tail, _ = process.communicate(timeout=10)
            output.extend(tail)
            text = output.decode(errors='replace')
            if process.returncode != 0:
                raise RuntimeError(f'ARMv6 application shutdown exit {process.returncode}: {text}')
            if 'lepton camera ready' not in text.lower() or 'backend selected: sample' not in text.lower():
                raise RuntimeError(f'Application did not report recorded-camera readiness: {text}')
            log = args.output.with_suffix('.log')
            log.write_text(text)
            result = {'result': 'PASS', 'binary_sha256': manifest['binary_sha256'],
                      'cpu': 'arm1176', 'emulator': subprocess.check_output(['qemu-arm', '--version'], text=True).splitlines()[0],
                      'duration_seconds': args.seconds, 'returncode': process.returncode,
                      'virtual_uart': True, 'camera_backend': 'sample', 'payload_ready': True,
                      'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
                      'library_path': env['LD_LIBRARY_PATH'], 'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest(),
                      'physical_hardware': False}
            args.output.write_text(json.dumps(result, indent=2) + '\n')
            return result
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
            os.close(master)
            os.close(slave)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--sysroot', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=5)
    args = parser.parse_args()
    if not 3 <= args.seconds <= 30:
        parser.error('--seconds must be 3..30')
    print(json.dumps(run(args), indent=2))
