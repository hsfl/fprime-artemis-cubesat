import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
BRIDGES = (
    (
        REPO_ROOT
        / "GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.hpp",
        REPO_ROOT
        / "GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.cpp",
    ),
    (
        REPO_ROOT
        / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/relay_uart_rf.hpp",
        REPO_ROOT
        / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/relay_uart_rf.cpp",
    ),
)


class RfMessageIdSequenceTests(unittest.TestCase):
    def test_both_bridges_allocate_message_ids_per_channel(self) -> None:
        for header_path, source_path in BRIDGES:
            header = header_path.read_text()
            source = source_path.read_text()

            self.assertIn(
                "uint8_t m_nextMsgId[link_protocol::CHANNEL_COUNT];",
                header,
            )
            self.assertIn("memset(m_nextMsgId, 0, sizeof(m_nextMsgId));", source)
            self.assertIn("m_nextMsgId[channel]++", source)
            self.assertNotIn("m_nextMsgId++", source)


if __name__ == "__main__":
    unittest.main()
