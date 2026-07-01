#!/usr/bin/env python3
"""Generate F Prime and Teensy transport-constant headers from one manifest."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "config/transport_constants.json"

OUTPUTS = {
    "fprime": ROOT / "ArtemisRpiTeensy_N2/Components/LinkCfg/LinkCfg.hpp",
    "satellite": ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp",
    "ground": ROOT / "GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp",
}


def hex_byte(value: int) -> str:
    return f"0x{value:02X}"


def cpp_string(value: str) -> str:
    return json.dumps(value)


def cpp_char(value: str) -> str:
    if len(value) != 1:
        raise ValueError(f"expected one-character value, got {value!r}")
    if value == "'":
        return r"'\''"
    if value == "\\":
        return r"'\\'"
    return f"'{value}'"


def generated_notice() -> str:
    return (
        "// Generated from config/transport_constants.json by "
        "tools/generate_transport_constants.py.\n"
        "// Do not hand-edit constants here; update the manifest and regenerate.\n\n"
    )


def render_fprime(cfg: dict) -> str:
    frame = cfg["frame"]
    channels = cfg["channels"]
    rpc = cfg["teensy_rpc"]
    rf = cfg["rf"]
    payload = cfg["payload"]
    return f"""#ifndef Components_LinkCfg_HPP
#define Components_LinkCfg_HPP

{generated_notice()}#include <Fw/FPrimeBasicTypes.hpp>

namespace Components {{
namespace LinkCfg {{

static constexpr U8 CHANNEL_CCSDS = {channels["ccsds"]};
static constexpr U8 CHANNEL_PAYLOAD = {channels["payload"]};
static constexpr U8 CHANNEL_TEENSY_LOCAL = {channels["teensy_local"]};
static constexpr U8 CHANNEL_COUNT = {channels["satellite_count"]};

static constexpr U8 TEENSY_TARGET_PDU = {rpc["target_pdu"]};
static constexpr U8 TEENSY_TARGET_RF_STATUS = {rpc["target_rf_status"]};
static constexpr U8 TEENSY_STATUS_OK = {rpc["status_ok"]};
static constexpr U8 TEENSY_STATUS_BAD_REQUEST = {rpc["status_bad_request"]};
static constexpr U8 TEENSY_STATUS_BUSY = {rpc["status_busy"]};
static constexpr U8 TEENSY_STATUS_TIMEOUT = {rpc["status_timeout"]};
static constexpr U8 TEENSY_STATUS_TARGET_ERROR = {rpc["status_target_error"]};
static constexpr U8 TEENSY_RF_OP_LINK_STATS = {rpc["rf_op_link_stats"]};

static constexpr U8 UART_FRAME_MAGIC_0 = {hex_byte(frame["magic_0"])};
static constexpr U8 UART_FRAME_MAGIC_1 = {hex_byte(frame["magic_1"])};
static constexpr FwSizeType UART_FRAME_MAX_PAYLOAD = {frame["max_payload"]};
static constexpr FwSizeType UART_FRAME_OVERHEAD = 7;
static constexpr FwSizeType UART_FRAME_MAX_ENCODED =
    UART_FRAME_MAX_PAYLOAD + UART_FRAME_OVERHEAD;

static constexpr FwSizeType RF_PACKET_MAX_LEN = {rf["packet_max_len"]};
static constexpr FwSizeType RF_SEGMENT_HEADER_LEN = {rf["segment_header_len"]};
static constexpr FwSizeType RF_SEGMENT_MAX_DATA_BYTES =
    RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
static constexpr FwSizeType PAYLOAD_PACKET_MAX_BYTES = RF_SEGMENT_MAX_DATA_BYTES;
static constexpr FwSizeType PAYLOAD_PACKET_DATA_BYTES = {payload["packet_data_bytes"]};
static constexpr U8 PAYLOAD_MAGIC_0 = {hex_byte(payload["magic_0"])};  // 'N'
static constexpr U8 PAYLOAD_MAGIC_1 = {hex_byte(payload["magic_1"])};  // '2'

inline bool isValidChannel(const U8 channel) {{
    return channel < CHANNEL_COUNT;
}}

}}  // namespace LinkCfg
}}  // namespace Components

#endif
"""


def render_teensy(cfg: dict, *, satellite: bool) -> str:
    frame = cfg["frame"]
    channels = cfg["channels"]
    rpc = cfg["teensy_rpc"]
    rf = cfg["rf"]
    command = cfg["command"]
    count = channels["satellite_count"] if satellite else channels["ground_count"]
    local_channel = ""
    local_rpc = ""
    if satellite:
        local_channel = f"static constexpr uint8_t CHANNEL_TEENSY_LOCAL = {channels['teensy_local']};\n"
        local_rpc = f"""
static constexpr uint8_t TEENSY_TARGET_PDU = {rpc["target_pdu"]};
static constexpr uint8_t TEENSY_TARGET_RF_STATUS = {rpc["target_rf_status"]};
static constexpr uint8_t TEENSY_STATUS_OK = {rpc["status_ok"]};
static constexpr uint8_t TEENSY_STATUS_BAD_REQUEST = {rpc["status_bad_request"]};
static constexpr uint8_t TEENSY_STATUS_BUSY = {rpc["status_busy"]};
static constexpr uint8_t TEENSY_STATUS_TIMEOUT = {rpc["status_timeout"]};
static constexpr uint8_t TEENSY_STATUS_TARGET_ERROR = {rpc["status_target_error"]};
static constexpr uint8_t TEENSY_RF_OP_LINK_STATS = {rpc["rf_op_link_stats"]};
"""

    return f"""#ifndef ARTEMIS_TEENSY_LINK_PROTOCOL_HPP
#define ARTEMIS_TEENSY_LINK_PROTOCOL_HPP

{generated_notice()}#include <Arduino.h>

namespace link_protocol {{

static constexpr uint8_t FRAME_MAGIC_0 = {hex_byte(frame["magic_0"])};
static constexpr uint8_t FRAME_MAGIC_1 = {hex_byte(frame["magic_1"])};
static constexpr uint8_t CHANNEL_CCSDS = {channels["ccsds"]};
static constexpr uint8_t CHANNEL_PAYLOAD = {channels["payload"]};
{local_channel}static constexpr uint8_t CHANNEL_RF_COUNT = {channels["rf_count"]};
static constexpr uint8_t CHANNEL_COUNT = {count};
{local_rpc}
// UART wrapper payload carries opaque bytes tagged by virtual channel.
static constexpr uint16_t FRAME_MAX_PAYLOAD = {frame["max_payload"]};
static constexpr uint32_t FRAME_TIMEOUT_MS = {frame["timeout_ms"]};

// RF segmentation parameters.
static constexpr uint8_t RF_SEGMENT_MAGIC_CCSDS = {hex_byte(rf["segment_magic_ccsds"])};
static constexpr uint8_t RF_SEGMENT_MAGIC_PAYLOAD = {hex_byte(rf["segment_magic_payload"])};
static constexpr uint8_t RF_ACK_SEGMENT_INDEX = {hex_byte(rf["ack_segment_index"])};
static constexpr uint8_t RF_PACKET_MAX_LEN = {rf["packet_max_len"]};
static constexpr uint8_t RF_SEGMENT_HEADER_LEN = {rf["segment_header_len"]};
static constexpr uint8_t RF_SEGMENT_MAX_DATA = RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
static constexpr uint32_t RF_REASSEMBLY_TIMEOUT_MS = {rf["reassembly_timeout_ms"]};
static constexpr uint8_t RF_INTER_SEGMENT_GAP_MS = {rf["inter_segment_gap_ms"]};
static constexpr uint8_t RF_ACK_RETRIES = {rf["ack_retries"]};
static constexpr uint16_t RF_ACK_TIMEOUT_MS = {rf["ack_timeout_ms"]};

static constexpr char COMMAND_PREFIX = {cpp_char(command["prefix"])};
static constexpr size_t COMMAND_MAX_LEN = {command["max_len"]};

static constexpr const char* CMD_PING = {cpp_string(command["ping"])};
static constexpr const char* CMD_LINK_STATUS = {cpp_string(command["link_status"])};
static constexpr const char* CMD_RESET_COUNTERS = {cpp_string(command["reset_counters"])};

static constexpr const char* RESP_PONG = {cpp_string(command["pong_response"])};
static constexpr const char* RESP_RESET_OK = {cpp_string(command["reset_ok_response"])};

inline bool isValidChannel(uint8_t channel) {{
  return channel < CHANNEL_COUNT;
}}

inline bool isRfChannel(uint8_t channel) {{
  return channel < CHANNEL_RF_COUNT;
}}

inline uint8_t magicForChannel(uint8_t channel) {{
  return channel == CHANNEL_PAYLOAD ? RF_SEGMENT_MAGIC_PAYLOAD : RF_SEGMENT_MAGIC_CCSDS;
}}

inline bool channelForMagic(uint8_t magic, uint8_t& channel) {{
  if (magic == RF_SEGMENT_MAGIC_CCSDS) {{
    channel = CHANNEL_CCSDS;
    return true;
  }}
  if (magic == RF_SEGMENT_MAGIC_PAYLOAD) {{
    channel = CHANNEL_PAYLOAD;
    return true;
  }}
  return false;
}}

}}  // namespace link_protocol

#endif
"""


def render_all(cfg: dict) -> dict[Path, str]:
    return {
        OUTPUTS["fprime"]: render_fprime(cfg),
        OUTPUTS["satellite"]: render_teensy(cfg, satellite=True),
        OUTPUTS["ground"]: render_teensy(cfg, satellite=False),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if generated headers are not up to date")
    args = parser.parse_args()

    cfg = json.loads(MANIFEST.read_text())
    generated = render_all(cfg)

    if args.check:
        stale = []
        for path, content in generated.items():
            if not path.exists() or path.read_text() != content:
                stale.append(path)
        if stale:
            print("transport generated headers are stale:", file=sys.stderr)
            for path in stale:
                print(f"- {path.relative_to(ROOT)}", file=sys.stderr)
            print("Run: python3 tools/generate_transport_constants.py", file=sys.stderr)
            return 1
        print("transport generated headers OK")
        return 0

    for path, content in generated.items():
        path.write_text(content)
        print(f"wrote {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
