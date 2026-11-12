"""Profile separation, physical-artifact and runtime guard regressions."""
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('deployment_profiles', ROOT / 'tools/deployments.py')
profiles = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profiles)


class ProfileTests(unittest.TestCase):
    def test_presets_preserve_hardware_boundary(self):
        presets = json.loads((ROOT / 'fprime-artemis-core/CMakePresets.json').read_text())
        by_name = {p['name']: p for p in presets['configurePresets']}
        for name in profiles.PROFILES:
            expected = profiles.PROFILES[name]['flight_controller']
            actual = by_name[name]['cacheVariables']
            self.assertEqual(actual['BOARD'], expected['board'])
            self.assertEqual(actual['ARTEMIS_TWIN'], 'ON' if expected['twin'] else 'OFF')
            self.assertEqual(actual['ARTEMIS_GROUND_LINK'], 'RFM23')
        self.assertNotEqual(by_name['yamcs-twin']['binaryDir'], by_name['yamcs-rfm23-hil']['binaryDir'])

    def test_serial_profile_requires_distinct_explicit_ports(self):
        args = argparse.Namespace(output=ROOT / '.profiles/test', ccsds_port=None, science_port=None)
        with self.assertRaisesRegex(ValueError, 'explicit'):
            profiles.hil_ground_command(args)
        args.ccsds_port = '/dev/pts/1'; args.science_port = '/dev/pts/1'
        with self.assertRaisesRegex(ValueError, 'different'):
            profiles.hil_ground_command(args)

    def test_hil_build_never_starts_before_required_sysroot(self):
        with patch.object(profiles, 'run') as run:
            code = profiles.main(['yamcs-rfm23-hil', 'build'])
        self.assertEqual(code, 2)
        run.assert_not_called()

    def test_native_twin_elf_refused_for_hardware(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            elf = tmp / 'native'; elf.write_bytes(b'\x7fELF\x02\x01' + b'\0' * 30)
            with self.assertRaisesRegex(ValueError, '32-bit'):
                profiles.normalize('pi', elf, tmp / 'dict.json', tmp / 'out')
            self.assertFalse((tmp / 'out').exists())

    def test_pi_service_is_explicit_real_camera_and_excludes_twin(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            profiles.write_pi_service(tmp, 'pi', '/home/pi/payload', '/dev/serial0')
            launch = (tmp / 'run-payload.sh').read_text()
            self.assertIn('unset ARTEMIS_TWIN C3M_LEPTON_SAMPLE_CSV', launch)
            self.assertIn('LEPTON_CAMERA_BACKEND=uvc', launch)
            self.assertIn('EnvironmentFile=/home/pi/payload/config/payload.env', (tmp / 'config/artemis-payload.service').read_text())
            with self.assertRaises(ValueError):
                profiles.write_pi_service(tmp, 'pi', '/home/pi/$(bad)', '/dev/serial0')


if __name__ == '__main__':
    unittest.main()
