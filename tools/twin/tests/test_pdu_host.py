import importlib.util
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import crc16

ROOT = Path(__file__).resolve().parents[3]


def request(opcode, sequence, payload=b""):
    body = bytes((2, 0, opcode, sequence, 0, len(payload))) + payload
    return b"\xa5" + body + crc16(body).to_bytes(2, "little")


class ActualPduHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("cc"):
            raise unittest.SkipTest("host C compiler unavailable")
        cls.temporary = tempfile.TemporaryDirectory()
        copied = Path(cls.temporary.name)
        source = ROOT / "external/artemis-pdu"
        for name in ("app.c", "app.h", "pdu_packet.c", "pdu_packet.h", "pdu_protocol_v2.h", "version.h"):
            shutil.copy2(source / "src" / name, copied / name)
        spec = importlib.util.spec_from_file_location("pdu_build_metadata_test", source / "ArtemisPDU.X/tools/update_build_info.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        (copied / "pdu_build_info.h").write_text(module.render_header(source))
        cls.binary = copied / "pdu-host"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-D_POSIX_C_SOURCE=200809L",
                        "-I" + str(source / "tests/host/include"), "-I" + str(copied),
                        str(ROOT / "tools/twin/pdu_host.c"), str(copied / "app.c"), str(copied / "pdu_packet.c"),
                        "-o", str(cls.binary)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.process = subprocess.Popen([str(self.binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def tearDown(self):
        self.process.terminate()
        self.process.wait(timeout=2)
        self.process.stdin.close()
        self.process.stdout.close()

    def exchange(self, frame):
        self.process.stdin.write(frame)
        self.process.stdin.flush()
        data = bytearray()
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.05)
            if readable:
                data.extend(os.read(self.process.stdout.fileno(), 128))
                if len(data) >= 7 and len(data) >= 9 + data[6]:
                    total = 9 + data[6]
                    result = bytes(data[:total])
                    self.assertEqual(result[0], 0xA5)
                    self.assertEqual(crc16(result[1:-2]), int.from_bytes(result[-2:], "little"))
                    return result
        self.fail("Actual PDU C parser did not respond within two seconds")

    def test_actual_parser_info_and_gpio_state_round_trip(self):
        response = self.exchange(request(1, 41))
        self.assertEqual(response[1:6], bytes((2, 1, 1, 41, 0)))
        on = self.exchange(request(0x11, 42, bytes((1, 1))))
        self.assertEqual(on[5], 0)
        state = self.exchange(request(0x10, 43, bytes((1,))))
        self.assertEqual(state[5], 0)
        self.assertEqual(state[7:9], bytes((1, 1)))
        off = self.exchange(request(0x11, 44, bytes((1, 0))))
        self.assertEqual(off[5], 0)
        state = self.exchange(request(0x10, 45, bytes((1,))))
        self.assertEqual(state[7:9], bytes((1, 0)))

    def test_corrupt_frame_does_not_prevent_later_valid_request(self):
        bad = bytearray(request(0, 50))
        bad[-1] ^= 1
        self.process.stdin.write(bad)
        self.process.stdin.flush()
        time.sleep(0.02)
        reply = self.exchange(request(0, 51))
        self.assertEqual(reply[4], 51)
        self.assertEqual(reply[5], 0)


if __name__ == "__main__":
    unittest.main()
