#!/usr/bin/env python3
"""Inspect an ARMv6 Linux artifact without executing it or contacting a target."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def command(*args: str) -> str:
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def target_path(root: Path, path: Path) -> Path:
    """Resolve target absolute symlinks within its sysroot, never the host /lib."""
    pending = list(path.relative_to(root).parts)
    resolved = []
    links = 0
    while pending:
        part = pending.pop(0)
        if part in ('', '.'):
            continue
        if part == '..':
            if resolved:
                resolved.pop()
            continue
        candidate = root.joinpath(*resolved, part)
        if candidate.is_symlink():
            links += 1
            if links > 40:
                raise RuntimeError(f'Symlink loop: {path}')
            link = candidate.readlink()
            if link.is_absolute():
                resolved = []
            pending = list(link.parts[1:] if link.is_absolute() else link.parts) + pending
        else:
            resolved.append(part)
    return root.joinpath(*resolved)


def verify(args: argparse.Namespace) -> dict:
    binary, dictionary, root = args.binary, args.dictionary, args.sysroot
    cache = (args.build_dir / 'CMakeCache.txt').read_text() if args.build_dir else ''
    cached = lambda name: re.search(r'^' + re.escape(name) + r':[^=]*=(.*)$', cache, re.M)
    libraries_match = cached('FPRIME_LIBRARY_LOCATIONS')
    libraries = libraries_match.group(1).split(';') if libraries_match else []
    twin_match = cached('ARTEMIS_TWIN')
    twin = twin_match.group(1).upper() in ('ON', 'TRUE', '1') if twin_match else False
    if args.project.name == 'fprime-artemis-core' and args.build_dir:
        if any('fprime-zephyr' in library for library in libraries) or twin:
            raise RuntimeError('Hardware Linux build imported Zephyr or enabled the twin build flag')
    header = command('readelf', '-h', str(binary))
    attrs = command('readelf', '-A', str(binary))
    program = command('readelf', '-l', str(binary))
    dynamic = command('readelf', '-d', str(binary))
    if not re.search(r'Machine:\s+ARM\s*$', header, re.M):
        raise RuntimeError('Expected an ARM ELF, not the container host executable')
    if not re.search(r'Tag_CPU_arch: v6(?:KZ)?\s*$', attrs, re.M):
        raise RuntimeError('Expected ARMv6 CPU attributes')
    if 'Tag_ABI_VFP_args: VFP registers' not in attrs:
        raise RuntimeError('Expected ARM hard-float ABI')
    match = re.search(r'Requesting program interpreter:\s*([^\]]+)', program)
    if not match or match.group(1) != '/lib/ld-linux-armhf.so.3':
        raise RuntimeError('Unexpected target interpreter')
    interpreter = target_path(root, root / match.group(1).lstrip('/'))
    if not interpreter.is_file():
        raise RuntimeError('Target sysroot lacks the program interpreter')
    need = lambda text: re.findall(r'\(NEEDED\).*\[([^\]]+)\]', text)
    needed = need(dynamic)
    if args.require_uvc and not any(name.startswith('libuvc.so') for name in needed):
        raise RuntimeError('Hardware Pi artifact did not link the required UVC backend')
    # Use the same explicit library precedence as the QEMU check. The prepared
    # camera sysroot may contain a newer libuvc under /usr/local than distro libuvc.
    search_paths = ['/usr/local/lib', '/lib/arm-linux-gnueabihf', '/usr/lib/arm-linux-gnueabihf', '/lib', '/usr/lib']
    directories = [root / path.lstrip('/') for path in search_paths]
    directories += sorted((root / 'usr/lib/gcc/arm-linux-gnueabihf').glob('*'), reverse=True)
    resolved = {}
    pending = list(needed)
    while pending:
        name = pending.pop()
        if name in resolved:
            continue
        found = None
        for directory in directories:
            candidate = target_path(root, directory / name)
            if candidate.is_file():
                found = candidate
                break
        if found is None:
            raise RuntimeError(f'Missing target shared library: {name}')
        lib_header = command('readelf', '-h', str(found))
        if not re.search(r'Machine:\s+ARM\s*$', lib_header, re.M):
            raise RuntimeError(f'Non-ARM shared library in sysroot: {found}')
        lib_attrs = command('readelf', '-A', str(found))
        cpu = re.search(r'Tag_CPU_arch:\s*(\S+)', lib_attrs)
        if cpu and cpu.group(1) not in ('v4', 'v4T', 'v5T', 'v5TE', 'v5TEJ', 'v6', 'v6K', 'v6KZ'):
            raise RuntimeError(f'Incompatible library CPU architecture {cpu.group(1)}: {found}')
        resolved[name] = {'sysroot_path': str(found.relative_to(root)), 'sha256': sha(found),
                          'cpu_arch': cpu.group(1) if cpu else 'not recorded'}
        pending.extend(need(command('readelf', '-d', str(found))))
    # Parse the dictionary before publishing it. Payload-specific checks apply
    # only to that deployment, preserving the wrapper's other legacy targets.
    model = json.loads(dictionary.read_text())
    if not isinstance(model.get('commands'), list):
        raise RuntimeError('Expected an F Prime command dictionary')
    if args.deployment == 'PayloadComputerDeployment' and not any('payloadManager' in value.get('name', '') for value in model['commands']):
        raise RuntimeError('Expected payload-computer topology dictionary')
    prefix = ['git', '-c', 'safe.directory=*', '-C', str(args.project)]
    source = command(*prefix, 'rev-parse', 'HEAD').strip()
    status = command(*prefix, 'status', '--porcelain')
    result = {'target': 'armv6-linux-gnueabihf', 'toolchain': 'pi-zero-w-armv6hf',
              'binary_path': str(binary), 'dictionary_path': str(dictionary),
              'binary_sha256': sha(binary), 'dictionary_sha256': sha(dictionary),
              'require_uvc': args.require_uvc, 'uvc_enabled': any(n.startswith('libuvc.so') for n in needed),
              'cmake_library_locations': libraries, 'twin_build': twin,
              'source_sha': source, 'source_dirty': bool(status),
              'framework_sha': command('git', '-c', 'safe.directory=*', '-C', str(args.project / 'lib/fprime'), 'rev-parse', 'HEAD').strip(),
              'interpreter': match.group(1), 'needed': needed, 'resolved_dependencies': resolved,
              'library_search_paths': search_paths,
              'dependency_resolution': 'explicit search paths; confirm the same installed libraries on the physical Pi',
              'file': command('file', str(binary)).strip(), 'arm_attributes': attrs,
              'runtime_execution': 'not performed; ELF inspection only',
              'sysroot_metadata': (root / 'metadata.txt').read_text() if (root / 'metadata.txt').exists() else None}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('binary', 'dictionary', 'sysroot', 'project', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--require-uvc', action='store_true')
    parser.add_argument('--deployment', default='')
    parser.add_argument('--build-dir', type=Path)
    print(json.dumps(verify(parser.parse_args()), indent=2))
