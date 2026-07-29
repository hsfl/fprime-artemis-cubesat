import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SOAK_SKETCH = (
    REPO_ROOT
    / "GDS_Teensy/firmware/gds_tx_thermal_soak/gds_tx_thermal_soak.ino"
)


class GdsTxThermalSoakTests(unittest.TestCase):
    def test_autonomous_soak_is_tx_only_at_full_test_packet_size(self) -> None:
        sketch = SOAK_SKETCH.read_text()
        self.assertIn("RH_RF22_RF23BP_TXPOW_30DBM", sketch)
        self.assertIn("RF_PACKET_BYTES = 49", sketch)
        self.assertIn("g_profile.start_in_receive = false", sketch)
        self.assertIn("artemis::rf23bp::sendPacket", sketch)
        self.assertNotIn("receivePacket", sketch)

    def test_led_blinks_for_tx_and_latches_solid_for_a_stall(self) -> None:
        sketch = SOAK_SKETCH.read_text()
        self.assertIn("STATUS_LED_PIN = 13", sketch)
        self.assertIn("TX_LED_TOGGLE_MS = 250", sketch)
        self.assertIn("void indicateTxSuccess()", sketch)
        self.assertIn("void setLedStalled()", sketch)
        self.assertIn("indicateTxSuccess();", sketch)
        self.assertIn("setLedStalled();", sketch)


if __name__ == "__main__":
    unittest.main()
