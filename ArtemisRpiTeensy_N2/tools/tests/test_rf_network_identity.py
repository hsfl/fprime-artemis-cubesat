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

    def test_rf_tx_completion_is_bounded_recoverable_and_observable(self) -> None:
        self.assertEqual(self.transport["rf"]["tx_complete_timeout_ms"], 500)
        identity = generator.resolve_rf_identity(self.transport, self.registry)
        self.assertIn(
            "RF_TX_COMPLETE_TIMEOUT_MS = 500",
            generator.render_fprime(self.transport, identity),
        )
        for satellite in (False, True):
            self.assertIn(
                "RF_TX_COMPLETE_TIMEOUT_MS = 500",
                generator.render_teensy(self.transport, identity, satellite=satellite),
            )

        helper_paths = (
            REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/libs/rf23bp/artemis_rf23bp.hpp",
            REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/artemis_rf23bp.hpp",
            REPO_ROOT / "GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp",
        )
        fifo_clear_pulse = re.compile(
            r"spiWrite\(RH_RF22_REG_08_OPERATING_MODE2,\s*"
            r"op_mode2 \| RH_RF22_FFCLRTX \| RH_RF22_FFCLRRX\);\s*"
            r"radio\.spiWrite\(RH_RF22_REG_08_OPERATING_MODE2, op_mode2\);"
        )
        for helper_path in helper_paths:
            with self.subTest(helper=helper_path):
                helper = helper_path.read_text()
                for outcome in ("SENT", "START_FAILED", "TX_TIMEOUT"):
                    self.assertIn(outcome, helper)
                self.assertNotIn("radio.waitPacketSent();", helper)
                self.assertIn("waitPacketSent(tx_complete_timeout_ms)", helper)
                self.assertIn("tx_complete_timeout_ms == 0", helper)
                self.assertRegex(helper, fifo_clear_pulse)
                self.assertIn("setModeRx();", helper)

        roots = (
            REPO_ROOT / "GDS_Teensy/firmware/gds_teensy",
            REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy",
        )
        for root in roots:
            with self.subTest(root=root):
                driver = (root / "src/rf23_driver.cpp").read_text()
                relay = (root / "src/relay_uart_rf.cpp").read_text()
                counters = (root / "src/link_counters.hpp").read_text()
                debug = next(root.glob("*.ino")).read_text()
                self.assertIn("link_protocol::RF_TX_COMPLETE_TIMEOUT_MS", driver)
                for member, field in (
                    ("rfTxTimeouts", "rf_tx_timeouts="),
                    ("rfRecoveries", "rf_recoveries="),
                    ("rfTxTerminalFailures", "rf_tx_terminal_failures="),
                ):
                    self.assertIn(member, counters)
                    self.assertIn(member, relay)
                    self.assertIn(field, relay)
                    self.assertIn(field, debug)

        ground_driver = (roots[0] / "src/rf23_driver.cpp").read_text()
        self.assertIn("initRadio(m_radio, m_radioPins, m_radioProfile, &SerialUSB1)", ground_driver)
        self.assertIn("&SerialUSB1);", ground_driver)

    def test_satellite_radio_shutdown_and_pi_first_contract(self) -> None:
        helper_paths = (
            REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/libs/rf23bp/artemis_rf23bp.hpp",
            REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/artemis_rf23bp.hpp",
            REPO_ROOT / "GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp",
        )
        helper_bytes = [path.read_bytes() for path in helper_paths]
        self.assertEqual(helper_bytes[0], helper_bytes[1])
        self.assertEqual(helper_bytes[0], helper_bytes[2])
        helper = helper_bytes[0].decode()
        self.assertIn("uint8_t sdn_pin = 37", helper)
        self.assertIn("detachInterrupt(digitalPinToInterrupt(pins.irq_pin))", helper)
        self.assertIn("digitalWrite(pins.sdn_pin, HIGH)", helper)
        self.assertIn("digitalWrite(pins.sdn_pin, LOW)", helper)
        self.assertIn("probeDeviceIdentity", helper)
        self.assertNotIn("probeChipReady", helper)
        self.assertNotIn("spiWrite(RH_RF22_REG_07_OPERATING_MODE1, RH_RF22_SWRES)", helper)

        satellite_root = REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy"
        driver_header = (satellite_root / "src/rf23_driver.hpp").read_text()
        driver = (satellite_root / "src/rf23_driver.cpp").read_text()
        relay = (satellite_root / "src/relay_uart_rf.cpp").read_text()
        router = (satellite_root / "src/local_teensy_router.cpp").read_text()
        sketch = (satellite_root / "satellite_teensy.ino").read_text()
        for contract in (
            "beginSafeOff",
            "setEnabled",
            "failSafeOffLocalTx",
            "isReady",
            "lastAcceptedRssiAgeMs",
        ):
            self.assertIn(contract, driver_header)
        self.assertIn("return isReady() && m_radio.available()", driver)
        self.assertGreaterEqual(driver.count("if (!isReady())"), 2)
        self.assertGreaterEqual(relay.count("if (!m_rf.isReady())"), 2)
        self.assertIn("discardRadioWorkOnOff", relay)
        self.assertIn("resetReassembly(channel, false, partialMessage)", relay)
        self.assertIn("m_rf.failSafeOffLocalTx();", relay)
        self.assertIn("TEENSY_STATUS_TARGET_ERROR", router)
        self.assertIn("m_fault == link_protocol::TEENSY_RF_FAULT_LOCAL_TX", driver)
        self.assertIn("isReady() && m_fault == link_protocol::TEENSY_RF_FAULT_NONE", driver)
        self.assertIn("m_rssiValid = false;", driver)
        self.assertNotIn("g_rfDriver.begin();", sketch)
        self.assertIn("RADIO_SDN_PIN = 37", sketch)
        self.assertLess(
            sketch.index("g_rfDriver.beginSafeOff(watchdogReset)"),
            sketch.index("digitalWrite(RPI_ENABLE_PIN, HIGH)"),
        )
        self.assertLess(
            sketch.index("digitalWrite(RPI_ENABLE_PIN, HIGH)"),
            sketch.index("Serial2.begin(UART_BAUD)"),
        )


if __name__ == "__main__":
    unittest.main()
