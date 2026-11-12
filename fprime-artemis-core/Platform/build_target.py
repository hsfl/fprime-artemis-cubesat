#!/usr/bin/env python3
"""Configure/link a Teensy41 deployment with external tool and build directories."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', required=True, type=Path)
parser.add_argument('--tools-bin', required=True, type=Path, help='F Prime requirements + Zephyr tools virtualenv bin')
parser.add_argument('--build-dir', required=True, type=Path)
parser.add_argument('--ground-link', choices=('RFM23', 'USB'), default='RFM23')
parser.add_argument('--jobs', type=int, default=8)
args = parser.parse_args()
if not 1 <= args.jobs <= 32:
    parser.error('--jobs must be between 1 and 32')
project = Path(__file__).resolve().parents[1]
repo = project.parent
build = args.build_dir.resolve()
tools = args.tools_bin.resolve()
sdk = args.sdk.resolve()
if not sdk.is_dir() or not (tools / 'python').is_file():
    parser.error('SDK directory and tool virtualenv python must exist')
if build == project or project in build.parents:
    parser.error('Use a build directory outside the source project to avoid stale copied caches')
env = os.environ.copy()
env['PATH'] = str(tools) + os.pathsep + env['PATH']
env['ZEPHYR_SDK_INSTALL_DIR'] = str(sdk)
commands = [
    ['cmake', '-S', str(project), '-B', str(build), '-G', 'Ninja',
     f'-DFPRIME_PROJECT_ROOT={project}', f'-DFPRIME_FRAMEWORK_PATH={project}/lib/fprime',
     f'-DFPRIME_LIBRARY_LOCATIONS={project}/lib/fprime-zephyr;{repo}/tools/cross',
     '-DFPRIME_ENABLE_FRAMEWORK_UTS=OFF', '-DFPRIME_ENABLE_AUTOCODER_UTS=OFF',
     f'-DCMAKE_TOOLCHAIN_FILE={project}/lib/fprime-zephyr/cmake/toolchain/zephyr.cmake',
     '-DBOARD=teensy41', f'-DARTEMIS_GROUND_LINK={args.ground_link}', '-DARTEMIS_TWIN=OFF',
     f'-DUSER_CACHE_DIR={build}/.cache', f'-DPython3_EXECUTABLE={tools}/python',
     f'-DPYTHON_EXECUTABLE={tools}/python', f'-DCMAKE_MAKE_PROGRAM={tools}/ninja',
     f'-DFPRIME_INSTALL_DEST={build}/artifacts'],
    ['cmake', '--build', str(build), '--target',
     'FprimeArtemisCore_Deployments_FlightControllerDeployment', '--parallel', str(args.jobs)],
    [str(tools / 'python'), str(project / 'Platform/report_memory.py'), str(build / 'zephyr/zephyr.elf')],
]
for command in commands:
    print('+', ' '.join(command), flush=True)
    subprocess.run(command, env=env, check=True)
