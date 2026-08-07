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

    def test_neutron2_identity_is_strict_and_uses_existing_radiohead_header(self) -> None:
        identity = generator.resolve_rf_identity(self.transport, self.registry)

        self.assertEqual(identity["network_key"], "neutron2")
        self.assertNotEqual(identity["network_id"], self.registry["networks"]["epscorc3m"]["id"])
        self.assertNotEqual(identity["ground_address"], identity["satellite_address"])
        self.assertEqual(identity["ground_profile"], "n2-gds-a")
        self.assertEqual(identity["spacecraft_profile"], "n2-spacecraft-a")
        self.assertEqual(self.transport["rf"]["packet_max_len"], 49)
        self.assertEqual(self.transport["rf"]["segment_header_len"], 5)

        ground = generator.render_teensy(self.transport, self.registry, satellite=False)
        satellite = generator.render_teensy(self.transport, self.registry, satellite=True)
        self.assertIn("classifyRfHeader", ground)
        self.assertIn("WRONG_NETWORK", ground)
        self.assertIn("WRONG_ADDRESS", ground)
        self.assertIn("WRONG_VERSION", ground)
        self.assertIn("RF_ENDPOINT_PROFILE == RF_PROFILE_N2_GDS_B", ground)
        self.assertIn("RF_ENDPOINT_PROFILE == RF_PROFILE_N2_SPACECRAFT_B", satellite)
        self.assertIn("RF_LOCAL_ADDRESS = 0xA1", ground)
        self.assertIn("RF_REMOTE_ADDRESS = 0xA2", ground)
        self.assertIn("RF_LOCAL_ADDRESS = 0xA2", satellite)
        self.assertIn("RF_REMOTE_ADDRESS = 0xA1", satellite)

    def test_all_named_endpoint_profiles_match_the_allocated_tuples(self) -> None:
        expected = {
            "c3m-gds": ("ground", 0xC3, 0xA1, 0xA2),
            "c3m-spacecraft": ("spacecraft", 0xC3, 0xA2, 0xA1),
            "n2-gds-a": ("ground", 0xD2, 0xA1, 0xA2),
            "n2-spacecraft-a": ("spacecraft", 0xD2, 0xA2, 0xA1),
            "n2-gds-b": ("ground", 0xD2, 0xA4, 0xA3),
            "n2-spacecraft-b": ("spacecraft", 0xD2, 0xA3, 0xA4),
        }
        self.assertEqual(set(self.registry["endpoint_profiles"]), set(expected))
        for profile_key, values in expected.items():
            with self.subTest(profile=profile_key):
                endpoint = generator.resolve_endpoint_profile(self.registry, profile_key)
                self.assertEqual(
                    (
                        endpoint["role"],
                        endpoint["network_id"],
                        endpoint["local_address"],
                        endpoint["remote_address"],
                    ),
                    values,
                )

        spacecraft_a = generator.resolve_endpoint_profile(self.registry, "n2-spacecraft-a")
        spacecraft_b = generator.resolve_endpoint_profile(self.registry, "n2-spacecraft-b")
        self.assertEqual(spacecraft_a["ccsds_spacecraft_id"], 0x44)
        self.assertEqual(spacecraft_b["ccsds_spacecraft_id"], 0x45)
        self.assertEqual(spacecraft_a["payload_namespace"], "n2-a")
        self.assertEqual(spacecraft_b["payload_namespace"], "n2-b")
        self.assertNotEqual(spacecraft_a["ccsds_spacecraft_id"], spacecraft_b["ccsds_spacecraft_id"])

        gds_a = generator.resolve_endpoint_profile(self.registry, "n2-gds-a")
        gds_b = generator.resolve_endpoint_profile(self.registry, "n2-gds-b")
        self.assertEqual((gds_a["gds_session"], gds_a["gds_gui_port"]), ("n2-a", 5050))
        self.assertEqual((gds_b["gds_session"], gds_b["gds_gui_port"]), ("n2-b", 5051))

    def test_channel_2_extended_status_preserves_legacy_develop_contract(self) -> None:
        rpc = self.transport["teensy_rpc"]
        self.assertEqual(rpc["rf_op_link_stats"], 1)
        self.assertNotEqual(rpc["rf_op_status"], rpc["rf_op_link_stats"])
        self.assertNotEqual(rpc["rf_op_set_enabled"], rpc["rf_op_link_stats"])

        router = (
            REPO_ROOT
            / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/local_teensy_router.cpp"
        ).read_text()
        header = (
            REPO_ROOT
            / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/local_teensy_router.hpp"
        ).read_text()
        self.assertIn("TEENSY_RF_OP_LINK_STATS", router)
        self.assertIn("prepareLegacyRfStatsResponse(requestId)", router)
        self.assertIn("RF_LEGACY_STATS_PAYLOAD_LEN = 21", header)
        self.assertIn("TEENSY_RF_OP_STATUS", router)
        self.assertIn("RF_STATUS_PAYLOAD_LEN = 33", header)

    def test_duplicate_network_ids_are_rejected(self) -> None:
        registry = copy.deepcopy(self.registry)
        registry["networks"]["neutron2"]["id"] = registry["networks"]["epscorc3m"]["id"]
        with self.assertRaisesRegex(ValueError, "unique"):
            generator.resolve_rf_identity(self.transport, registry)

    def test_unknown_selected_network_is_rejected(self) -> None:
        transport = copy.deepcopy(self.transport)
        transport["rf"]["endpoint_profiles"]["ground"] = "unknown"
        with self.assertRaisesRegex(ValueError, "endpoint profile"):
            generator.resolve_rf_identity(transport, self.registry)

    def test_cross_pair_selection_is_rejected(self) -> None:
        transport = copy.deepcopy(self.transport)
        transport["rf"]["endpoint_profiles"]["spacecraft"] = "n2-spacecraft-b"
        with self.assertRaisesRegex(ValueError, "paired tuple"):
            generator.resolve_rf_identity(transport, self.registry)

    def test_duplicate_address_within_one_network_is_rejected(self) -> None:
        registry = copy.deepcopy(self.registry)
        registry["endpoint_profiles"]["n2-gds-b"]["local_address"] = 0xA1
        with self.assertRaisesRegex(ValueError, "unique within network"):
            generator.validate_rf_registry(registry)

    def test_generated_fprime_configs_assign_unique_spacecraft_ids(self) -> None:
        generated = generator.render_all(self.transport, self.registry)
        configs = {
            path.name: content
            for path, content in generated.items()
            if path.name.startswith("ComCfg.n2-spacecraft-")
        }
        self.assertIn("dictionary constant SpacecraftId = 0x0044", configs["ComCfg.n2-spacecraft-a.fpp"])
        self.assertIn("dictionary constant SpacecraftId = 0x0045", configs["ComCfg.n2-spacecraft-b.fpp"])

        cmake = (
            REPO_ROOT
            / "ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig/CMakeLists.txt"
        ).read_text()
        self.assertIn('NEUTRON2_SPACECRAFT_PROFILE "n2-spacecraft-a"', cmake)
        self.assertIn("ComCfg.n2-spacecraft-a.fpp", cmake)
        self.assertIn("ComCfg.n2-spacecraft-b.fpp", cmake)

    def test_build_and_runtime_tools_keep_node_profiles_isolated(self) -> None:
        script_expectations = {
            "GDS_Teensy/tools/arduino-cli/build.sh": ("n2-gds-a", "RF_ENDPOINT_PROFILE", "/$RF_PROFILE"),
            "GDS_Teensy/tools/arduino-cli/upload.sh": ("n2-gds-a", "--profile", "/$RF_PROFILE"),
            "ArtemisTeensy_N2_Baremetal/tools/arduino-cli/build.sh": (
                "n2-spacecraft-a",
                "RF_ENDPOINT_PROFILE",
                "/$RF_PROFILE",
            ),
            "ArtemisTeensy_N2_Baremetal/tools/arduino-cli/upload.sh": (
                "n2-spacecraft-a",
                "--profile",
                "/$RF_PROFILE",
            ),
        }
        for relative, needles in script_expectations.items():
            with self.subTest(script=relative):
                content = (REPO_ROOT / relative).read_text()
                for needle in needles:
                    self.assertIn(needle, content)

        payload_paths = (
            REPO_ROOT / "ArtemisRpiTeensy_N2/Components/LinkCfg/PayloadPaths.hpp"
        ).read_text()
        self.assertIn("NEUTRON_PAYLOAD_CAPTURE_DIR", payload_paths)
        self.assertIn("NEUTRON_PAYLOAD_SIM_CURSOR", payload_paths)
        for profile, namespace in (("n2-spacecraft-a", "n2-a"), ("n2-spacecraft-b", "n2-b")):
            env_file = (REPO_ROOT / f"deploy/pi/profiles/{profile}.env").read_text()
            self.assertIn(f"NEUTRON_SPACECRAFT_PROFILE={profile}", env_file)
            self.assertIn(f"/tmp/neutron_payload_captures/{namespace}", env_file)

        gds_launcher = (REPO_ROOT / "ArtemisRpiTeensy_N2/tools/run_gds_uart.sh").read_text()
        self.assertIn("--session", gds_launcher)
        self.assertIn("logs/gds/$SESSION", gds_launcher)

    def test_generated_classifier_executes_all_strict_cases_for_every_profile(self) -> None:
        headers = {
            "ground": REPO_ROOT / "GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp",
            "spacecraft": REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp",
        }
        for profile_key, profile in self.registry["endpoint_profiles"].items():
            endpoint = generator.resolve_endpoint_profile(self.registry, profile_key)
            header = headers[profile["role"]]
            with self.subTest(profile=profile_key), tempfile.TemporaryDirectory() as tmp:
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
  if (link_protocol::RF_NETWORK_ID != {endpoint['network_id']}) return 5;
  if (link_protocol::RF_LOCAL_ADDRESS != {endpoint['local_address']}) return 6;
  if (link_protocol::RF_REMOTE_ADDRESS != {endpoint['remote_address']}) return 7;
  return 0;
}}
''',
                    encoding="utf-8",
                )
                executable = root / "identity_test"
                subprocess.run(
                    [
                        "c++",
                        "-std=c++17",
                        f"-DRF_ENDPOINT_PROFILE={endpoint['profile_macro']}",
                        "-I",
                        str(root),
                        "-I",
                        str(header.parent),
                        str(source),
                        "-o",
                        str(executable),
                    ],
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
                generator.render_teensy(self.transport, self.registry, satellite=satellite),
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
        self.assertIn("&SerialUSB1,", ground_driver)
        self.assertIn("&attemptSnapshot);", ground_driver)

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
        self.assertIn("class BoundedRf22", helper)
        self.assertIn("initBounded", helper)
        self.assertIn("chip_ready_timeout_ms", helper)
        self.assertIn("spiWrite(RH_RF22_REG_07_OPERATING_MODE1, RH_RF22_SWRES)", helper)
        self.assertNotIn(
            "while (!(spiRead(RH_RF22_REG_04_INTERRUPT_STATUS2) & RH_RF22_ICHIPRDY))",
            helper,
        )

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
        self.assertIn("consumeTxTimeoutRecoveryRequest", relay)
        self.assertIn("failSafeOffLocalTx();", driver)
        self.assertIn("TEENSY_STATUS_TARGET_ERROR", router)
        self.assertIn("m_fault == link_protocol::TEENSY_RF_FAULT_LOCAL_TX", driver)
        self.assertIn("isReady() && m_fault == link_protocol::TEENSY_RF_FAULT_NONE", driver)
        self.assertIn("m_rssiValid = false;", driver)
        self.assertIn("const bool radioOk = g_rfDriver.begin();", sketch)
        self.assertIn("RADIO_SDN_PIN = 37", sketch)
        self.assertLess(
            sketch.index("g_rfDriver.beginSafeOff()"),
            sketch.index("digitalWrite(RPI_ENABLE_PIN, HIGH)"),
        )
        self.assertLess(
            sketch.index("digitalWrite(RPI_ENABLE_PIN, HIGH)"),
            sketch.index("Serial2.begin(UART_BAUD)"),
        )


if __name__ == "__main__":
    unittest.main()
