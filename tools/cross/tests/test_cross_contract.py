"""Cross wrapper must not contact targets in local-only profile builds."""
import json
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / 'tools/cross/cross_compile.sh'
SPEC = importlib.util.spec_from_file_location('verify_artifact', ROOT / 'tools/cross/verify_artifact.py')
VERIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFIER)


class LocalOnlyTests(unittest.TestCase):
    def test_local_only_rejects_requested_ssh_sync(self):
        result = subprocess.run([str(SCRIPT), '--local-only', '--resync-sysroot'], text=True, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('SSH is disabled', result.stderr)

    def test_external_sysroot_is_mounted_read_only_and_hardware_flag_propagates(self):
        with tempfile.TemporaryDirectory(prefix='cross test ') as directory:
            base = Path(directory)
            sysroot = base / 'target sysroot'
            (sysroot / 'usr/lib/arm-linux-gnueabihf').mkdir(parents=True)
            (sysroot / 'usr/lib/gcc/arm-linux-gnueabihf/12').mkdir(parents=True)
            (sysroot / 'lib').mkdir()
            (sysroot / 'lib/ld-linux-armhf.so.3').write_text('test loader')
            binaries = base / 'bin'
            binaries.mkdir()
            record = base / 'docker.json'
            docker = binaries / 'docker'
            docker.write_text('#!/usr/bin/env python3\nimport json,os,sys\nfrom pathlib import Path\nPath(os.environ["CROSS_TEST_RECORD"]).write_text(json.dumps(sys.argv[1:]))\nraise SystemExit(42)\n')
            docker.chmod(0o755)
            for command in ('ssh', 'scp', 'rsync'):
                target = binaries / command
                target.write_text('#!/bin/sh\necho UNEXPECTED_HARDWARE_ACCESS >&2\nexit 99\n')
                target.chmod(0o755)
            env = os.environ.copy()
            env['PATH'] = str(binaries) + os.pathsep + env['PATH']
            env['CROSS_TEST_RECORD'] = str(record)
            result = subprocess.run([str(SCRIPT), '--local-only', '--skip-sync', '--skip-image-build',
                                     '--require-uvc', '--project', 'fprime-artemis-core', '--deployment',
                                     'PayloadComputerDeployment', '--sysroot', str(sysroot)],
                                    text=True, capture_output=True, env=env)
            self.assertEqual(result.returncode, 42, result.stdout + result.stderr)
            self.assertNotIn('UNEXPECTED_HARDWARE_ACCESS', result.stderr)
            arguments = json.loads(record.read_text())
            self.assertEqual(arguments[0], 'run')
            self.assertIn(str(sysroot) + ':/sysroot:ro', arguments)
            self.assertIn('REQUIRE_UVC=true', arguments)
            self.assertTrue((sysroot / 'lib/ld-linux-armhf.so.3').is_file())


class SysrootTests(unittest.TestCase):
    def test_target_absolute_directory_and_file_symlinks_stay_in_sysroot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'usr/lib').mkdir(parents=True)
            library = root / 'usr/lib/loader.so'
            library.write_text('target fixture')
            (root / 'lib').symlink_to('/usr/lib')
            (root / 'usr/lib/loader').symlink_to('/lib/loader.so')
            self.assertEqual(VERIFIER.target_path(root, root / 'lib/loader'), library)

    def test_target_symlink_loop_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'loop').symlink_to('/loop')
            with self.assertRaisesRegex(RuntimeError, 'Symlink loop'):
                VERIFIER.target_path(root, root / 'loop')


if __name__ == '__main__':
    unittest.main()
