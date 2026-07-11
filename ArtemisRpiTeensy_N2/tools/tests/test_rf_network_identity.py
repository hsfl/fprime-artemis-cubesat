import copy
import importlib.util
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
GENERATOR_PATH = REPO_ROOT / "tools" / "generate_transport_constants.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("rf_identity_generator", GENERATOR_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {GENERATOR_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


generator = load_generator()


class RfNetworkIdentityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.transport = json.loads((REPO_ROOT / "config" / "transport_constants.json").read_text())
        self.registry = json.loads((REPO_ROOT / "config" / "rf_networks.json").read_text())

    def test_c3m_identity_is_strict_and_uses_existing_radiohead_header(self) -> None:
        identity = generator.resolve_rf_identity(self.transport, self.registry)

        self.assertEqual(identity["network_key"], "epscorc3m")
        self.assertNotEqual(identity["network_id"], self.registry["networks"]["neutron2"]["id"])
        self.assertNotEqual(identity["ground_address"], identity["satellite_address"])
        self.assertEqual(self.transport["rf"]["packet_max_len"], 49)
        self.assertEqual(self.transport["rf"]["segment_header_len"], 5)

        ground = generator.render_teensy(self.transport, identity, satellite=False)
        satellite = generator.render_teensy(self.transport, identity, satellite=True)
        self.assertIn("classifyRfHeader", ground)
        self.assertIn("WRONG_NETWORK", ground)
        self.assertIn("WRONG_ADDRESS", ground)
        self.assertIn("WRONG_VERSION", ground)
        self.assertIn("RF_LOCAL_ADDRESS = 0xA1", ground)
        self.assertIn("RF_REMOTE_ADDRESS = 0xA2", ground)
        self.assertIn("RF_LOCAL_ADDRESS = 0xA2", satellite)
        self.assertIn("RF_REMOTE_ADDRESS = 0xA1", satellite)

    def test_duplicate_network_ids_are_rejected(self) -> None:
        registry = copy.deepcopy(self.registry)
        registry["networks"]["neutron2"]["id"] = registry["networks"]["epscorc3m"]["id"]
        with self.assertRaisesRegex(ValueError, "unique"):
            generator.resolve_rf_identity(self.transport, registry)

    def test_unknown_selected_network_is_rejected(self) -> None:
        transport = copy.deepcopy(self.transport)
        transport["rf"]["network"] = "unknown"
        with self.assertRaisesRegex(ValueError, "not defined"):
            generator.resolve_rf_identity(transport, self.registry)

    def test_generated_classifier_executes_all_strict_cases_for_both_roles(self) -> None:
        for relative in (
            "GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp",
            "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp",
        ):
            header = REPO_ROOT / relative
            with self.subTest(header=relative), tempfile.TemporaryDirectory() as tmp:
                root = pathlib.Path(tmp)
                (root / "Arduino.h").write_text(
                    "#include <cstddef>\n#include <cstdint>\nusing std::size_t;\n",
                    encoding="utf-8",
                )
                source = root / "identity_test.cpp"
                source.write_text(
                    f'''#include "{header.name}"
int main() {{
  using link_protocol::RfHeaderStatus;
  if (link_protocol::classifyRfHeader(
          link_protocol::RF_LOCAL_ADDRESS,
          link_protocol::RF_REMOTE_ADDRESS,
          link_protocol::RF_NETWORK_ID,
          link_protocol::RF_PROTOCOL_VERSION) != RfHeaderStatus::ACCEPT) return 1;
  if (link_protocol::classifyRfHeader(
          link_protocol::RF_LOCAL_ADDRESS,
          link_protocol::RF_REMOTE_ADDRESS,
          static_cast<uint8_t>(link_protocol::RF_NETWORK_ID + 1U),
          link_protocol::RF_PROTOCOL_VERSION) != RfHeaderStatus::WRONG_NETWORK) return 2;
  if (link_protocol::classifyRfHeader(
          link_protocol::RF_REMOTE_ADDRESS,
          link_protocol::RF_LOCAL_ADDRESS,
          link_protocol::RF_NETWORK_ID,
          link_protocol::RF_PROTOCOL_VERSION) != RfHeaderStatus::WRONG_ADDRESS) return 3;
  if (link_protocol::classifyRfHeader(
          link_protocol::RF_LOCAL_ADDRESS,
          link_protocol::RF_REMOTE_ADDRESS,
          link_protocol::RF_NETWORK_ID,
          static_cast<uint8_t>(link_protocol::RF_PROTOCOL_VERSION + 1U)) != RfHeaderStatus::WRONG_VERSION) return 4;
  return 0;
}}
''',
                    encoding="utf-8",
                )
                executable = root / "identity_test"
                subprocess.run(
                    ["c++", "-std=c++17", "-I", str(root), "-I", str(header.parent), str(source), "-o", str(executable)],
                    check=True,
                    capture_output=True,
                    text=True,
                )
                subprocess.run([str(executable)], check=True)

    def test_driver_and_relay_paths_enforce_identity_before_forwarding(self) -> None:
        roots = (
            REPO_ROOT / "GDS_Teensy/firmware/gds_teensy/src",
            REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src",
        )
        for root in roots:
            with self.subTest(root=root):
                driver = (root / "rf23_driver.cpp").read_text()
                relay = (root / "relay_uart_rf.cpp").read_text()
                for setup_call in (
                    "setPromiscuous(true)",
                    "setHeaderTo(link_protocol::RF_REMOTE_ADDRESS)",
                    "setHeaderFrom(link_protocol::RF_LOCAL_ADDRESS)",
                    "setHeaderId(link_protocol::RF_NETWORK_ID)",
                    "setHeaderFlags(link_protocol::RF_PROTOCOL_VERSION, 0xFF)",
                ):
                    self.assertIn(setup_call, driver)
                self.assertEqual(relay.count("const Rf23ReceiveResult result = m_rf.recv"), 2)
                guarded_receives = re.findall(
                    r"const Rf23ReceiveResult result = m_rf\.recv\([^;]+;\s+"
                    r"if \(acceptRfReceiveResult\(result\) && rfLen > 0\)",
                    relay,
                )
                self.assertEqual(len(guarded_receives), 2)


if __name__ == "__main__":
    unittest.main()
