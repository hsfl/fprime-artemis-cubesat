from __future__ import annotations

import pathlib
import subprocess
import unittest


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = PROJECT_ROOT / "tools" / "run_c3m_local_demo.sh"


class C3mLocalDemoScriptTest(unittest.TestCase):
    def run_script(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["bash", str(SCRIPT), *arguments],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_shell_syntax_is_valid(self) -> None:
        result = subprocess.run(
            ["bash", "-n", str(SCRIPT)],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_help_advertises_repeatable_capture_cycles(self) -> None:
        result = self.run_script("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--captures <count>", result.stdout)
        self.assertIn("ground payload receiver", result.stdout)
        self.assertNotIn("--capture-seconds", result.stdout)

    def test_capture_count_must_be_positive(self) -> None:
        result = self.run_script("--captures", "0", "--exit-after-sequence")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--captures must be a positive integer", result.stderr)

    def test_loss_injection_index_is_bounded(self) -> None:
        result = self.run_script(
            "--drop-payload-data-index", "65536", "--exit-after-sequence"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "--drop-payload-data-index must be an integer from 0 through 65535",
            result.stderr,
        )

    def test_receiver_restart_cycle_must_be_requested(self) -> None:
        result = self.run_script(
            "--captures", "2", "--restart-receiver-cycle", "3", "--exit-after-sequence"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "--restart-receiver-cycle must identify one requested capture cycle",
            result.stderr,
        )

    def test_abandon_first_cycle_requires_a_following_capture(self) -> None:
        result = self.run_script(
            "--captures", "1", "--abandon-first-cycle", "--exit-after-sequence"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "--abandon-first-cycle requires --captures 2 or greater",
            result.stderr,
        )

    def test_c3m_sequence_omits_unneeded_conops_commands(self) -> None:
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertNotIn('send_command "payloadDriverLepton.ENABLE"', source)
        self.assertNotIn('send_command "scienceApp.CONFIGURE_CAPTURE_DURATION"', source)
        self.assertNotIn('send_command "storageManager.REPORT_LATEST_DATASET"', source)
        self.assertNotIn('send_command "storageManager.REPORT_STORAGE_HISTORY"', source)
        self.assertIn('send_command "missionApp.SCHEDULE_COLLECTION"', source)
        self.assertIn('send_command "commsApp.REQUEST_SCIENCE_DOWNLINK"', source)
        self.assertIn('tools/payload_receiver.py', source)
        self.assertIn('--drop-payload-data-index', source)
        self.assertIn('--checkpoint-dir', source)
        self.assertIn('--blackhole-payload-data-index-first-transfer', source)
        self.assertIn('PARTIAL_EXPECTED', source)


if __name__ == "__main__":
    unittest.main()
