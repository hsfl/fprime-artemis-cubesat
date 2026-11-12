#!/usr/bin/env python3
"""Named local-twin and RFM23BP HIL build/runtime profiles. Never flashes or deploys."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
PROFILES = json.loads((ROOT / 'config/deployment_profiles.json').read_text())['profiles']


def run(command: list[str]) -> None:
    print('+', ' '.join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def load_builder():
    spec = importlib.util.spec_from_file_location('twin_build', ROOT / 'tools/twin/build.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def publish(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_name(target.name + '.new')
    shutil.copy2(source, tmp)
    tmp.replace(target)


def normalize(role: str, binary: Path, dictionary: Path, output: Path, *, hex_file: Path | None = None,
              cross_validation: dict | None = None) -> None:
    builder = load_builder()
    header = binary.read_bytes()[:20]
    if len(header) != 20 or header[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', header, 18)[0] != 40:
        raise ValueError('HIL artifacts require 32-bit little-endian ARM ELF files, not native twin binaries')
    output.mkdir(parents=True, exist_ok=True)
    executable = output / 'bin' / ('flight-controller' if role == 'fc' else 'payload-computer')
    normalized_dictionary = output / 'dict/topology.json'
    publish(binary, executable)
    publish(dictionary, normalized_dictionary)
    state = builder.git_state(ROOT)
    manifest = {'role': role, 'deployment_profile': 'yamcs-rfm23-hil',
                'target': 'teensy41' if role == 'fc' else 'pi-zero-w-armv6hf',
                'source_sha': state['sha'], 'source_dirty': state['dirty'],
                'working_source_sha256': builder.working_source_digest(),
                'framework_sha': builder.source_sha(builder.PROJECT / 'lib/fprime'),
                'zephyr_sha': builder.source_sha(builder.PROJECT / 'lib/zephyr-workspace/zephyr'),
                'dependency_states': {n: builder.git_state(builder.PROJECT / 'lib' / n)
                                      for n in ('fprime', 'fprime-zephyr', 'zephyr-workspace/zephyr')},
                'pdu_dependency': {**builder.git_state(ROOT / 'external/artemis-pdu'),
                                  'protocol_sha256': hashlib.sha256((ROOT / 'external/artemis-pdu/src/pdu_protocol_v2.h').read_bytes()).hexdigest()},
                'binary_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
                'dictionary_sha256': hashlib.sha256(normalized_dictionary.read_bytes()).hexdigest()}
    if hex_file is not None:
        publish(hex_file, output / 'firmware/zephyr.hex')
        manifest['hex_sha256'] = hashlib.sha256(hex_file.read_bytes()).hexdigest()
    if role == 'pi':
        if not cross_validation or not cross_validation.get('uvc_enabled') or not cross_validation.get('require_uvc'):
            raise ValueError('HIL Pi artifacts require successful ARMv6 and real-UVC cross validation')
        if cross_validation.get('binary_sha256') != manifest['binary_sha256'] or cross_validation.get('dictionary_sha256') != manifest['dictionary_sha256']:
            raise ValueError('Cross validation does not identify these Pi artifacts')
        manifest['uvc_enabled'] = True
        manifest['cross_validation'] = cross_validation
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def write_pi_service(output: Path, user: str, remote_root: str, uart: str) -> None:
    if not user.replace('_', '').replace('-', '').isalnum() or not user[0].isalpha():
        raise ValueError('Pi user must be a simple Linux account name')
    for value in (remote_root, uart):
        if not value.startswith('/') or any(c.isspace() or c in '\"\'`$%\\' for c in value):
            raise ValueError('Pi root and UART must be absolute paths without whitespace or shell/systemd expansion')
    config = output / 'config'
    config.mkdir(parents=True, exist_ok=True)
    (config / 'payload.env').write_text(f'ARTEMIS_PAYLOAD_ROOT={remote_root}\nARTEMIS_FC_UART={uart}\nLEPTON_CAMERA_BACKEND=uvc\n')
    publish(ROOT / 'config/profiles/yamcs-rfm23-hil/run-payload.sh', output / 'run-payload.sh')
    (output / 'run-payload.sh').chmod(0o755)
    (config / 'artemis-payload.service').write_text(
        '[Unit]\nDescription=Artemis Pi payload computer\nAfter=local-fs.target\n\n[Service]\nType=simple\n'
        f'User={user}\nSupplementaryGroups=dialout video\nWorkingDirectory={remote_root}\n'
        f'EnvironmentFile={remote_root}/config/payload.env\nExecStart={remote_root}/run-payload.sh\n'
        'Restart=on-failure\nRestartSec=3\nKillSignal=SIGTERM\nUMask=0027\n\n[Install]\nWantedBy=multi-user.target\n')


def build_hil(args) -> None:
    base = args.output.resolve()
    if args.role in ('all', 'pi') and not args.sysroot:
        raise ValueError('HIL Pi build requires --sysroot (pre-synced locally; this command never uses SSH)')
    if args.role in ('all', 'fc'):
        if not args.sdk or not args.tools_bin:
            raise ValueError('HIL FC build requires --sdk and --tools-bin')
        build = base / 'build/fc'
        run([sys.executable, str(ROOT / 'fprime-artemis-core/Platform/build_target.py'),
             '--sdk', str(args.sdk.resolve()), '--tools-bin', str(args.tools_bin.resolve()),
             '--build-dir', str(build), '--ground-link', 'RFM23', '--jobs', str(args.jobs)])
        builder = load_builder()
        normalize('fc', build / 'zephyr/zephyr.elf', builder.select_dictionary(build, 'fc'), base / 'artifacts/fc',
                  hex_file=build / 'zephyr/zephyr.hex')
    if args.role in ('all', 'pi'):
        if not args.sysroot:
            raise ValueError('HIL Pi build requires --sysroot (pre-synced locally; this command never uses SSH)')
        run([str(ROOT / 'tools/cross/cross_compile.sh'), '--local-only', '--skip-sync',
             '--project', 'fprime-artemis-core', '--deployment', 'PayloadComputerDeployment',
             '--sysroot', str(args.sysroot.resolve()), '--require-uvc'])
        verify = ROOT / 'tools/cross/verify/fprime-artemis-core/PayloadComputerDeployment'
        def host_path(filename):
            value = (verify / filename).read_text().strip()
            return ROOT / value.removeprefix('/repo/') if value.startswith('/repo/') else Path(value)
        validation = json.loads((verify / 'manifest.json').read_text())
        normalize('pi', host_path('binary-path.txt'), host_path('dictionary-path.txt'), base / 'artifacts/pi',
                  cross_validation=validation)
        write_pi_service(base / 'artifacts/pi', args.pi_user, args.pi_root, args.pi_uart)
    print('HIL release artifacts:', base / 'artifacts')
    print('No firmware flashing, SSH copy, service installation or hardware operation performed.')


def hil_ground_command(args) -> list[str]:
    if not args.ccsds_port or not args.science_port:
        raise ValueError('Serial profile requires explicit --ccsds-port and --science-port; no automatic device selection')
    if os.path.realpath(args.ccsds_port) == os.path.realpath(args.science_port):
        raise ValueError('CCSDS and science must use different serial interfaces')
    base = args.output.resolve()
    run_id = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    data = args.data_dir or base / 'runs' / run_id
    return [str(args.ground_python or sys.executable), str(ROOT / 'tools/twin/ground.py'),
            '--transport', 'serial', '--ccsds-port', args.ccsds_port, '--science-port', args.science_port,
            '--fc-artifacts', str(base / 'artifacts/fc'), '--pi-artifacts', str(base / 'artifacts/pi'),
            '--data-dir', str(data.resolve()), '--yamcs-data', str((base / 'yamcs-data').resolve()),
            '--yamcs-port', str(args.yamcs_port), '--tcp-port', str(args.tcp_port), '--run-id', 'hil-' + run_id]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', choices=PROFILES)
    parser.add_argument('action', choices=('show', 'build', 'up', 'ground', 'down', 'status', 'logs', 'demo', 'test', 'faults'))
    parser.add_argument('--role', choices=('all', 'fc', 'pi'), default='all')
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--tools-bin', type=Path)
    parser.add_argument('--sysroot', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / '.profiles/yamcs-rfm23-hil')
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--pi-user', default='pi')
    parser.add_argument('--pi-root', default='/home/pi/artemis/payload-core')
    parser.add_argument('--pi-uart', default='/dev/serial0')
    parser.add_argument('--ccsds-port')
    parser.add_argument('--science-port')
    parser.add_argument('--ground-python', type=Path)
    parser.add_argument('--data-dir', type=Path)
    parser.add_argument('--yamcs-port', type=int, default=8090)
    parser.add_argument('--tcp-port', type=int, default=5100)
    parser.add_argument('--captures', type=int, default=3)
    parser.add_argument('--pi-off-repair', action='store_true')
    args = parser.parse_args(argv)
    if not 1 <= args.jobs <= 32 or not 1 <= args.yamcs_port <= 65535 or not 1 <= args.tcp_port <= 65535:
        parser.error('Invalid jobs or port range')
    if args.yamcs_port == args.tcp_port:
        parser.error('Yamcs HTTP and transport TCP ports must differ')
    try:
        if args.action == 'show':
            print(json.dumps(PROFILES[args.profile], indent=2))
        elif args.profile == 'yamcs-twin':
            if args.action == 'ground':
                raise ValueError('The twin ground service is started with up')
            command = [sys.executable, str(ROOT / 'tools/twin/twin.py'), args.action]
            if args.action == 'build' and args.role != 'all': command += ['--role', args.role]
            if args.action == 'demo': command += ['--captures', str(args.captures)]
            if args.action == 'demo' and args.pi_off_repair: command += ['--pi-off-repair']
            run(command)
        elif args.action == 'build':
            build_hil(args)
        elif args.action in ('up', 'ground'):
            run(hil_ground_command(args))
        else:
            raise ValueError('HIL supports show, build and foreground up/ground; stop ground with Ctrl-C. Use twin test/faults for local simulation.')
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
