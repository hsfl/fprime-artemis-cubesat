#!/usr/bin/env python3
"""Generate F Prime and Teensy transport-constant headers from one manifest."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "config/transport_constants.json"
RF_NETWORKS = ROOT / "config/rf_networks.json"

OUTPUTS = {
    "fprime": ROOT / "ArtemisRpiTeensy_N2/Components/LinkCfg/LinkCfg.hpp",
    "satellite": ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp",
    "ground": ROOT / "GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp",
    "zephyr": ROOT / "FprimeZephyrSatellite/include/SatelliteController/GeneratedProtocol.hpp",
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


def cpp_flag(value: bool) -> int:
    return 1 if value else 0


def generated_notice() -> str:
    return (
        "// Generated from config/transport_constants.json and config/rf_networks.json by "
        "tools/generate_transport_constants.py.\n"
        "// Do not hand-edit constants here; update the manifest and regenerate.\n\n"
    )


def resolve_rf_identity(cfg: dict, registry: dict) -> dict:
    networks = registry.get("networks", {})
    network_key = cfg["rf"].get("network")
    if network_key not in networks:
        raise ValueError(f"rf.network {network_key!r} is not defined in config/rf_networks.json")
    ids = [entry.get("id") for entry in networks.values()]
    if any(not isinstance(value, int) or not 1 <= value <= 254 for value in ids):
        raise ValueError("RF network IDs must be unique integers in the range 1..254")
    if len(ids) != len(set(ids)):
        raise ValueError("RF network IDs must be unique")
    addresses = registry.get("addresses", {})
    ground = addresses.get("ground")
    satellite = addresses.get("satellite")
    if not all(isinstance(value, int) and 1 <= value <= 254 for value in (ground, satellite)):
        raise ValueError("RF role addresses must be integers in the range 1..254")
    if ground == satellite:
        raise ValueError("RF ground and satellite role addresses must differ")
    version = registry.get("protocol_version")
    if not isinstance(version, int) or not 1 <= version <= 255:
        raise ValueError("RF protocol version must be an integer in the range 1..255")
    return {
        "network_key": network_key,
        "network_id": networks[network_key]["id"],
        "protocol_version": version,
        "ground_address": ground,
        "satellite_address": satellite,
    }


def render_fprime(cfg: dict, identity: dict) -> str:
    frame = cfg["frame"]
    channels = cfg["channels"]
    rpc = cfg["teensy_rpc"]
    rf = cfg["rf"]
    ground_ack = rf["ack_directions"]["ground_to_satellite"]
    satellite_ack = rf["ack_directions"]["satellite_to_ground"]
    payload = cfg["payload"]
    preview = cfg["lepton_preview"]
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
static constexpr U8 TEENSY_TARGET_PAYLOAD_CACHE = {rpc["target_payload_cache"]};
static constexpr U8 TEENSY_TARGET_LEPTON_PREVIEW = {rpc["target_lepton_preview"]};
static constexpr U8 TEENSY_STATUS_OK = {rpc["status_ok"]};
static constexpr U8 TEENSY_STATUS_BAD_REQUEST = {rpc["status_bad_request"]};
static constexpr U8 TEENSY_STATUS_BUSY = {rpc["status_busy"]};
static constexpr U8 TEENSY_STATUS_TIMEOUT = {rpc["status_timeout"]};
static constexpr U8 TEENSY_STATUS_TARGET_ERROR = {rpc["status_target_error"]};
static constexpr U8 TEENSY_RF_OP_LINK_STATS = {rpc["rf_op_link_stats"]};
static constexpr U8 TEENSY_RF_OP_STATUS = {rpc["rf_op_status"]};
static constexpr U8 TEENSY_RF_OP_SET_ENABLED = {rpc["rf_op_set_enabled"]};
static constexpr U8 TEENSY_RF_STATE_OFF = {rpc["rf_state_off"]};
static constexpr U8 TEENSY_RF_STATE_READY = {rpc["rf_state_ready"]};
static constexpr U8 TEENSY_RF_FAULT_NONE = {rpc["rf_fault_none"]};
static constexpr U8 TEENSY_RF_FAULT_INIT_FAILED = {rpc["rf_fault_init_failed"]};
static constexpr U8 TEENSY_RF_FAULT_WATCHDOG_RESET = {rpc["rf_fault_watchdog_reset"]};
static constexpr U8 TEENSY_RF_FAULT_LOCAL_TX = {rpc["rf_fault_local_tx"]};
static constexpr U8 TEENSY_RF_BOOT_FLAG_WATCHDOG = {rpc["rf_boot_flag_watchdog"]};
static constexpr U32 TEENSY_RF_RSSI_AGE_UNKNOWN_MS = {rpc["rf_rssi_age_unknown_ms"]};

static constexpr U8 UART_FRAME_MAGIC_0 = {hex_byte(frame["magic_0"])};
static constexpr U8 UART_FRAME_MAGIC_1 = {hex_byte(frame["magic_1"])};
static constexpr FwSizeType UART_FRAME_MAX_PAYLOAD = {frame["max_payload"]};
static constexpr U32 UART_BAUD = {frame["uart_baud"]};
static constexpr U32 UART_INTER_FRAME_MARGIN_US = {frame["inter_frame_margin_us"]};
static constexpr U32 UART_PAYLOAD_CACHE_MARGIN_US = {frame["payload_cache_margin_us"]};
static constexpr U32 UART_CCSDS_EXTRA_MARGIN_US = {frame["ccsds_extra_margin_us"]};
static constexpr FwSizeType UART_FRAME_OVERHEAD = 7;
static constexpr FwSizeType UART_FRAME_MAX_ENCODED =
    UART_FRAME_MAX_PAYLOAD + UART_FRAME_OVERHEAD;

static constexpr FwSizeType RF_PACKET_MAX_LEN = {rf["packet_max_len"]};
static constexpr U8 RF_NETWORK_ID = {hex_byte(identity["network_id"])};
static constexpr U8 RF_PROTOCOL_VERSION = {hex_byte(identity["protocol_version"])};
static constexpr U8 RF_GROUND_ADDRESS = {hex_byte(identity["ground_address"])};
static constexpr U8 RF_SATELLITE_ADDRESS = {hex_byte(identity["satellite_address"])};
static constexpr FwSizeType RF_SEGMENT_HEADER_LEN = {rf["segment_header_len"]};
static constexpr FwSizeType RF_SEGMENT_MAX_DATA_BYTES =
    RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
static constexpr U8 RF_INTER_SEGMENT_GAP_MS = {rf["inter_segment_gap_ms"]};
static constexpr U8 RF_PAYLOAD_INTER_PACKET_GAP_MS = {rf["payload_inter_packet_gap_ms"]};
static constexpr U16 RF_TX_COMPLETE_TIMEOUT_MS = {rf["tx_complete_timeout_ms"]};
static constexpr U8 RF_GROUND_TX_ACK_REQUIRED_CCSDS = {cpp_flag(ground_ack["ccsds"])};
static constexpr U8 RF_GROUND_TX_ACK_REQUIRED_PAYLOAD = {cpp_flag(ground_ack["payload"])};
static constexpr U8 RF_SATELLITE_TX_ACK_REQUIRED_CCSDS = {cpp_flag(satellite_ack["ccsds"])};
static constexpr U8 RF_SATELLITE_TX_ACK_REQUIRED_PAYLOAD = {cpp_flag(satellite_ack["payload"])};
static constexpr FwSizeType PAYLOAD_PACKET_MAX_BYTES = RF_SEGMENT_MAX_DATA_BYTES;
static constexpr FwSizeType PAYLOAD_PACKET_DATA_BYTES = {payload["packet_data_bytes"]};
static constexpr U32 PAYLOAD_CACHE_MAX_BYTES = {payload["cache_max_bytes"]};
static constexpr FwSizeType PAYLOAD_CACHE_CHUNK_BYTES = {payload["cache_chunk_bytes"]};
static constexpr U8 PAYLOAD_CACHE_RF_GAP_MS = {payload["cache_rf_gap_ms"]};
static constexpr U8 PAYLOAD_CACHE_OP_BEGIN = {payload["cache_op_begin"]};
static constexpr U8 PAYLOAD_CACHE_OP_CHUNK = {payload["cache_op_chunk"]};
static constexpr U8 PAYLOAD_CACHE_OP_COMMIT_AND_SEND = {payload["cache_op_commit_and_send"]};
static constexpr U8 PAYLOAD_CACHE_OP_ABORT = {payload["cache_op_abort"]};
static constexpr U8 PAYLOAD_CACHE_STATE_EMPTY = {payload["cache_state_empty"]};
static constexpr U8 PAYLOAD_CACHE_STATE_RECEIVING = {payload["cache_state_receiving"]};
static constexpr U8 PAYLOAD_CACHE_STATE_READY = {payload["cache_state_ready"]};
static constexpr U8 PAYLOAD_CACHE_STATE_SENDING = {payload["cache_state_sending"]};
static constexpr U8 PAYLOAD_CACHE_STATE_ERROR = {payload["cache_state_error"]};
static constexpr U32 PAYLOAD_PACKETS_PER_RUN = {payload["packets_per_run"]};
static constexpr U32 PAYLOAD_RETRY_PACKETS_PER_RUN = {payload["retry_packets_per_run"]};
static constexpr U8 PAYLOAD_MAGIC_0 = {hex_byte(payload["magic_0"])};  // 'N'
static constexpr U8 PAYLOAD_MAGIC_1 = {hex_byte(payload["magic_1"])};  // '2'

static constexpr U32 LEPTON_PREVIEW_MAX_FRAME_BYTES = {preview["max_frame_bytes"]};
static constexpr FwSizeType LEPTON_PREVIEW_CHUNK_BYTES = {preview["chunk_bytes"]};
static constexpr U8 LEPTON_PREVIEW_RF_GAP_MS = {preview["rf_gap_ms"]};
static constexpr U8 LEPTON_PREVIEW_WIDTH = {preview["width"]};
static constexpr U8 LEPTON_PREVIEW_HEIGHT = {preview["height"]};
static constexpr U8 LEPTON_PREVIEW_PIXEL_FORMAT_U8 = {preview["pixel_format_u8"]};
static constexpr U8 LEPTON_PREVIEW_OP_BEGIN = {preview["op_begin"]};
static constexpr U8 LEPTON_PREVIEW_OP_CHUNK = {preview["op_chunk"]};
static constexpr U8 LEPTON_PREVIEW_OP_COMMIT_AND_SEND = {preview["op_commit_and_send"]};
static constexpr U8 LEPTON_PREVIEW_OP_ABORT = {preview["op_abort"]};
static constexpr U8 LEPTON_PREVIEW_STATE_EMPTY = {preview["state_empty"]};
static constexpr U8 LEPTON_PREVIEW_STATE_RECEIVING = {preview["state_receiving"]};
static constexpr U8 LEPTON_PREVIEW_STATE_READY = {preview["state_ready"]};
static constexpr U8 LEPTON_PREVIEW_STATE_SENDING = {preview["state_sending"]};
static constexpr U8 LEPTON_PREVIEW_STATE_ERROR = {preview["state_error"]};
static constexpr U8 LEPTON_PREVIEW_WIRE_MAGIC_0 = {hex_byte(preview["wire_magic_0"])};
static constexpr U8 LEPTON_PREVIEW_WIRE_MAGIC_1 = {hex_byte(preview["wire_magic_1"])};
static constexpr U8 LEPTON_PREVIEW_WIRE_VERSION = {preview["wire_version"]};
static constexpr U8 LEPTON_PREVIEW_WIRE_TYPE_FRAGMENT = {preview["wire_type_fragment"]};

inline bool isValidChannel(const U8 channel) {{
    return channel < CHANNEL_COUNT;
}}

inline bool rfGroundTxAckRequiredForChannel(const U8 channel) {{
    return channel == CHANNEL_PAYLOAD
               ? RF_GROUND_TX_ACK_REQUIRED_PAYLOAD != 0
               : RF_GROUND_TX_ACK_REQUIRED_CCSDS != 0;
}}

inline bool rfSatelliteTxAckRequiredForChannel(const U8 channel) {{
    return channel == CHANNEL_PAYLOAD
               ? RF_SATELLITE_TX_ACK_REQUIRED_PAYLOAD != 0
               : RF_SATELLITE_TX_ACK_REQUIRED_CCSDS != 0;
}}

}}  // namespace LinkCfg
}}  // namespace Components

#endif
"""


def render_teensy(cfg: dict, identity: dict, *, satellite: bool) -> str:
    frame = cfg["frame"]
    channels = cfg["channels"]
    rpc = cfg["teensy_rpc"]
    rf = cfg["rf"]
    ground_ack = rf["ack_directions"]["ground_to_satellite"]
    satellite_ack = rf["ack_directions"]["satellite_to_ground"]
    tx_ack = satellite_ack if satellite else ground_ack
    rx_ack = ground_ack if satellite else satellite_ack
    payload = cfg["payload"]
    preview = cfg["lepton_preview"]
    command = cfg["command"]
    count = channels["satellite_count"] if satellite else channels["ground_count"]
    local_address = identity["satellite_address"] if satellite else identity["ground_address"]
    remote_address = identity["ground_address"] if satellite else identity["satellite_address"]
    local_channel = ""
    local_rpc = ""
    if satellite:
        local_channel = f"static constexpr uint8_t CHANNEL_TEENSY_LOCAL = {channels['teensy_local']};\n"
        local_rpc = f"""
static constexpr uint8_t TEENSY_TARGET_PDU = {rpc["target_pdu"]};
static constexpr uint8_t TEENSY_TARGET_RF_STATUS = {rpc["target_rf_status"]};
static constexpr uint8_t TEENSY_TARGET_PAYLOAD_CACHE = {rpc["target_payload_cache"]};
static constexpr uint8_t TEENSY_TARGET_LEPTON_PREVIEW = {rpc["target_lepton_preview"]};
static constexpr uint8_t TEENSY_STATUS_OK = {rpc["status_ok"]};
static constexpr uint8_t TEENSY_STATUS_BAD_REQUEST = {rpc["status_bad_request"]};
static constexpr uint8_t TEENSY_STATUS_BUSY = {rpc["status_busy"]};
static constexpr uint8_t TEENSY_STATUS_TIMEOUT = {rpc["status_timeout"]};
static constexpr uint8_t TEENSY_STATUS_TARGET_ERROR = {rpc["status_target_error"]};
static constexpr uint8_t TEENSY_RF_OP_LINK_STATS = {rpc["rf_op_link_stats"]};
static constexpr uint8_t TEENSY_RF_OP_STATUS = {rpc["rf_op_status"]};
static constexpr uint8_t TEENSY_RF_OP_SET_ENABLED = {rpc["rf_op_set_enabled"]};
static constexpr uint8_t TEENSY_RF_STATE_OFF = {rpc["rf_state_off"]};
static constexpr uint8_t TEENSY_RF_STATE_READY = {rpc["rf_state_ready"]};
static constexpr uint8_t TEENSY_RF_FAULT_NONE = {rpc["rf_fault_none"]};
static constexpr uint8_t TEENSY_RF_FAULT_INIT_FAILED = {rpc["rf_fault_init_failed"]};
static constexpr uint8_t TEENSY_RF_FAULT_WATCHDOG_RESET = {rpc["rf_fault_watchdog_reset"]};
static constexpr uint8_t TEENSY_RF_FAULT_LOCAL_TX = {rpc["rf_fault_local_tx"]};
static constexpr uint8_t TEENSY_RF_BOOT_FLAG_WATCHDOG = {rpc["rf_boot_flag_watchdog"]};
static constexpr uint32_t TEENSY_RF_RSSI_AGE_UNKNOWN_MS = {rpc["rf_rssi_age_unknown_ms"]};
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
static constexpr uint32_t UART_BAUD = {frame["uart_baud"]};
static constexpr uint32_t UART_INTER_FRAME_MARGIN_US = {frame["inter_frame_margin_us"]};
static constexpr uint32_t UART_PAYLOAD_CACHE_MARGIN_US = {frame["payload_cache_margin_us"]};
static constexpr uint32_t UART_CCSDS_EXTRA_MARGIN_US = {frame["ccsds_extra_margin_us"]};

// RF segmentation parameters.
static constexpr uint8_t RF_NETWORK_ID = {hex_byte(identity["network_id"])};
static constexpr uint8_t RF_PROTOCOL_VERSION = {hex_byte(identity["protocol_version"])};
static constexpr uint8_t RF_LOCAL_ADDRESS = {hex_byte(local_address)};
static constexpr uint8_t RF_REMOTE_ADDRESS = {hex_byte(remote_address)};
static constexpr uint8_t RF_SEGMENT_MAGIC_CCSDS = {hex_byte(rf["segment_magic_ccsds"])};
static constexpr uint8_t RF_SEGMENT_MAGIC_PAYLOAD = {hex_byte(rf["segment_magic_payload"])};
static constexpr uint8_t RF_ACK_SEGMENT_INDEX = {hex_byte(rf["ack_segment_index"])};
static constexpr uint8_t RF_PACKET_MAX_LEN = {rf["packet_max_len"]};
static constexpr uint8_t RF_SEGMENT_HEADER_LEN = {rf["segment_header_len"]};
static constexpr uint8_t RF_SEGMENT_MAX_DATA = RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
static constexpr uint32_t RF_REASSEMBLY_TIMEOUT_MS = {rf["reassembly_timeout_ms"]};
static constexpr uint8_t RF_INTER_SEGMENT_GAP_MS = {rf["inter_segment_gap_ms"]};
static constexpr uint8_t RF_PAYLOAD_INTER_PACKET_GAP_MS = {rf["payload_inter_packet_gap_ms"]};
static constexpr uint16_t RF_TX_COMPLETE_TIMEOUT_MS = {rf["tx_complete_timeout_ms"]};
static constexpr uint8_t RF_ACK_RETRIES = {rf["ack_retries"]};
static constexpr uint16_t RF_ACK_TIMEOUT_MS = {rf["ack_timeout_ms"]};
static constexpr uint8_t RF_TX_ACK_REQUIRED_CCSDS = {cpp_flag(tx_ack["ccsds"])};
static constexpr uint8_t RF_TX_ACK_REQUIRED_PAYLOAD = {cpp_flag(tx_ack["payload"])};
static constexpr uint8_t RF_RX_ACK_REQUIRED_CCSDS = {cpp_flag(rx_ack["ccsds"])};
static constexpr uint8_t RF_RX_ACK_REQUIRED_PAYLOAD = {cpp_flag(rx_ack["payload"])};

static constexpr uint32_t PAYLOAD_PACKETS_PER_RUN = {payload["packets_per_run"]};
static constexpr uint32_t PAYLOAD_RETRY_PACKETS_PER_RUN = {payload["retry_packets_per_run"]};
static constexpr uint8_t PAYLOAD_MAGIC_0 = {hex_byte(payload["magic_0"])};
static constexpr uint8_t PAYLOAD_MAGIC_1 = {hex_byte(payload["magic_1"])};
static constexpr uint8_t PAYLOAD_PACKET_DATA_BYTES = {payload["packet_data_bytes"]};
static constexpr uint32_t PAYLOAD_CACHE_MAX_BYTES = {payload["cache_max_bytes"]};
static constexpr uint16_t PAYLOAD_CACHE_CHUNK_BYTES = {payload["cache_chunk_bytes"]};
static constexpr uint8_t PAYLOAD_CACHE_RF_GAP_MS = {payload["cache_rf_gap_ms"]};
static constexpr uint8_t PAYLOAD_CACHE_OP_BEGIN = {payload["cache_op_begin"]};
static constexpr uint8_t PAYLOAD_CACHE_OP_CHUNK = {payload["cache_op_chunk"]};
static constexpr uint8_t PAYLOAD_CACHE_OP_COMMIT_AND_SEND = {payload["cache_op_commit_and_send"]};
static constexpr uint8_t PAYLOAD_CACHE_OP_ABORT = {payload["cache_op_abort"]};
static constexpr uint8_t PAYLOAD_CACHE_STATE_EMPTY = {payload["cache_state_empty"]};
static constexpr uint8_t PAYLOAD_CACHE_STATE_RECEIVING = {payload["cache_state_receiving"]};
static constexpr uint8_t PAYLOAD_CACHE_STATE_READY = {payload["cache_state_ready"]};
static constexpr uint8_t PAYLOAD_CACHE_STATE_SENDING = {payload["cache_state_sending"]};
static constexpr uint8_t PAYLOAD_CACHE_STATE_ERROR = {payload["cache_state_error"]};

static constexpr uint16_t LEPTON_PREVIEW_MAX_FRAME_BYTES = {preview["max_frame_bytes"]};
static constexpr uint16_t LEPTON_PREVIEW_CHUNK_BYTES = {preview["chunk_bytes"]};
static constexpr uint8_t LEPTON_PREVIEW_RF_GAP_MS = {preview["rf_gap_ms"]};
static constexpr uint8_t LEPTON_PREVIEW_WIDTH = {preview["width"]};
static constexpr uint8_t LEPTON_PREVIEW_HEIGHT = {preview["height"]};
static constexpr uint8_t LEPTON_PREVIEW_PIXEL_FORMAT_U8 = {preview["pixel_format_u8"]};
static constexpr uint8_t LEPTON_PREVIEW_OP_BEGIN = {preview["op_begin"]};
static constexpr uint8_t LEPTON_PREVIEW_OP_CHUNK = {preview["op_chunk"]};
static constexpr uint8_t LEPTON_PREVIEW_OP_COMMIT_AND_SEND = {preview["op_commit_and_send"]};
static constexpr uint8_t LEPTON_PREVIEW_OP_ABORT = {preview["op_abort"]};
static constexpr uint8_t LEPTON_PREVIEW_STATE_EMPTY = {preview["state_empty"]};
static constexpr uint8_t LEPTON_PREVIEW_STATE_RECEIVING = {preview["state_receiving"]};
static constexpr uint8_t LEPTON_PREVIEW_STATE_READY = {preview["state_ready"]};
static constexpr uint8_t LEPTON_PREVIEW_STATE_SENDING = {preview["state_sending"]};
static constexpr uint8_t LEPTON_PREVIEW_STATE_ERROR = {preview["state_error"]};
static constexpr uint8_t LEPTON_PREVIEW_WIRE_MAGIC_0 = {hex_byte(preview["wire_magic_0"])};
static constexpr uint8_t LEPTON_PREVIEW_WIRE_MAGIC_1 = {hex_byte(preview["wire_magic_1"])};
static constexpr uint8_t LEPTON_PREVIEW_WIRE_VERSION = {preview["wire_version"]};
static constexpr uint8_t LEPTON_PREVIEW_WIRE_TYPE_FRAGMENT = {preview["wire_type_fragment"]};

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

enum class RfHeaderStatus : uint8_t {{
  ACCEPT = 0,
  WRONG_NETWORK = 1,
  WRONG_ADDRESS = 2,
  WRONG_VERSION = 3,
}};

inline RfHeaderStatus classifyRfHeader(uint8_t to, uint8_t from, uint8_t id, uint8_t flags) {{
  if (id != RF_NETWORK_ID) {{
    return RfHeaderStatus::WRONG_NETWORK;
  }}
  if (to != RF_LOCAL_ADDRESS || from != RF_REMOTE_ADDRESS) {{
    return RfHeaderStatus::WRONG_ADDRESS;
  }}
  if (flags != RF_PROTOCOL_VERSION) {{
    return RfHeaderStatus::WRONG_VERSION;
  }}
  return RfHeaderStatus::ACCEPT;
}}

inline bool txAckRequiredForChannel(uint8_t channel) {{
  return channel == CHANNEL_PAYLOAD
             ? RF_TX_ACK_REQUIRED_PAYLOAD != 0
             : RF_TX_ACK_REQUIRED_CCSDS != 0;
}}

inline bool rxAckRequiredForChannel(uint8_t channel) {{
  return channel == CHANNEL_PAYLOAD
             ? RF_RX_ACK_REQUIRED_PAYLOAD != 0
             : RF_RX_ACK_REQUIRED_CCSDS != 0;
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


def render_zephyr(cfg: dict, identity: dict) -> str:
    """Render the Arduino-free constants used by the F Prime/Zephyr port."""
    frame = cfg["frame"]
    channels = cfg["channels"]
    rpc = cfg["teensy_rpc"]
    rf = cfg["rf"]
    payload = cfg["payload"]
    preview = cfg["lepton_preview"]
    satellite_ack = rf["ack_directions"]["satellite_to_ground"]
    ground_ack = rf["ack_directions"]["ground_to_satellite"]
    return f"""#ifndef ARTEMIS_FPRIME_ZEPHYR_GENERATED_PROTOCOL_HPP
#define ARTEMIS_FPRIME_ZEPHYR_GENERATED_PROTOCOL_HPP

{generated_notice()}#include <cstddef>
#include <cstdint>

namespace SatelliteController {{
namespace Generated {{
inline constexpr std::uint8_t FRAME_MAGIC_0 = {hex_byte(frame["magic_0"])};
inline constexpr std::uint8_t FRAME_MAGIC_1 = {hex_byte(frame["magic_1"])};
inline constexpr std::uint8_t CHANNEL_CCSDS = {channels["ccsds"]};
inline constexpr std::uint8_t CHANNEL_PAYLOAD = {channels["payload"]};
inline constexpr std::uint8_t CHANNEL_LOCAL = {channels["teensy_local"]};
inline constexpr std::size_t CHANNEL_COUNT = {channels["satellite_count"]};
inline constexpr std::size_t RF_CHANNEL_COUNT = {channels["rf_count"]};
inline constexpr std::size_t FRAME_MAX_PAYLOAD = {frame["max_payload"]};
inline constexpr std::uint32_t FRAME_TIMEOUT_MS = {frame["timeout_ms"]};
inline constexpr std::uint32_t UART_BAUD = {frame["uart_baud"]};
inline constexpr std::uint8_t RF_NETWORK_ID = {hex_byte(identity["network_id"])};
inline constexpr std::uint8_t RF_PROTOCOL_VERSION = {hex_byte(identity["protocol_version"])};
inline constexpr std::uint8_t RF_LOCAL_ADDRESS = {hex_byte(identity["satellite_address"])};
inline constexpr std::uint8_t RF_REMOTE_ADDRESS = {hex_byte(identity["ground_address"])};
inline constexpr std::uint8_t RF_MAGIC_CCSDS = {hex_byte(rf["segment_magic_ccsds"])};
inline constexpr std::uint8_t RF_MAGIC_PAYLOAD = {hex_byte(rf["segment_magic_payload"])};
inline constexpr std::uint8_t RF_ACK_INDEX = {hex_byte(rf["ack_segment_index"])};
inline constexpr std::size_t RF_PACKET_MAX_LEN = {rf["packet_max_len"]};
inline constexpr std::size_t RF_SEGMENT_HEADER_LEN = {rf["segment_header_len"]};
inline constexpr std::size_t RF_SEGMENT_MAX_DATA = RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
inline constexpr std::uint32_t RF_REASSEMBLY_TIMEOUT_MS = {rf["reassembly_timeout_ms"]};
inline constexpr std::uint32_t RF_INTER_SEGMENT_GAP_MS = {rf["inter_segment_gap_ms"]};
inline constexpr std::uint32_t RF_PAYLOAD_INTER_PACKET_GAP_MS = {rf["payload_inter_packet_gap_ms"]};
inline constexpr std::uint32_t RF_TX_COMPLETE_TIMEOUT_MS = {rf["tx_complete_timeout_ms"]};
inline constexpr std::uint8_t RF_ACK_RETRIES = {rf["ack_retries"]};
inline constexpr std::uint32_t RF_ACK_TIMEOUT_MS = {rf["ack_timeout_ms"]};
inline constexpr bool TX_ACK_REQUIRED_CCSDS = {str(satellite_ack["ccsds"]).lower()};
inline constexpr bool TX_ACK_REQUIRED_PAYLOAD = {str(satellite_ack["payload"]).lower()};
inline constexpr bool RX_ACK_REQUIRED_CCSDS = {str(ground_ack["ccsds"]).lower()};
inline constexpr bool RX_ACK_REQUIRED_PAYLOAD = {str(ground_ack["payload"]).lower()};
inline constexpr std::uint8_t TEENSY_TARGET_PDU = {rpc["target_pdu"]};
inline constexpr std::uint8_t TEENSY_TARGET_RF_STATUS = {rpc["target_rf_status"]};
inline constexpr std::uint8_t TEENSY_TARGET_PAYLOAD_CACHE = {rpc["target_payload_cache"]};
inline constexpr std::uint8_t TEENSY_TARGET_LEPTON_PREVIEW = {rpc["target_lepton_preview"]};
inline constexpr std::uint8_t TEENSY_STATUS_OK = {rpc["status_ok"]};
inline constexpr std::uint8_t TEENSY_STATUS_BAD_REQUEST = {rpc["status_bad_request"]};
inline constexpr std::uint8_t TEENSY_STATUS_BUSY = {rpc["status_busy"]};
inline constexpr std::uint8_t TEENSY_STATUS_TIMEOUT = {rpc["status_timeout"]};
inline constexpr std::uint8_t TEENSY_STATUS_TARGET_ERROR = {rpc["status_target_error"]};
inline constexpr std::uint8_t TEENSY_RF_OP_STATUS = {rpc["rf_op_status"]};
inline constexpr std::uint8_t TEENSY_RF_OP_SET_ENABLED = {rpc["rf_op_set_enabled"]};
inline constexpr std::uint32_t TEENSY_RF_RSSI_AGE_UNKNOWN_MS = {rpc["rf_rssi_age_unknown_ms"]};
inline constexpr std::uint8_t PAYLOAD_CACHE_OP_BEGIN = {payload["cache_op_begin"]};
inline constexpr std::uint8_t PAYLOAD_CACHE_OP_CHUNK = {payload["cache_op_chunk"]};
inline constexpr std::uint8_t PAYLOAD_CACHE_OP_COMMIT_AND_SEND = {payload["cache_op_commit_and_send"]};
inline constexpr std::uint8_t PAYLOAD_CACHE_OP_ABORT = {payload["cache_op_abort"]};
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_EMPTY = {payload["cache_state_empty"]};
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_RECEIVING = {payload["cache_state_receiving"]};
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_READY = {payload["cache_state_ready"]};
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_SENDING = {payload["cache_state_sending"]};
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_ERROR = {payload["cache_state_error"]};
inline constexpr std::uint8_t PAYLOAD_MAGIC_0 = {hex_byte(payload["magic_0"])};
inline constexpr std::uint8_t PAYLOAD_MAGIC_1 = {hex_byte(payload["magic_1"])};
inline constexpr std::uint8_t PAYLOAD_PACKET_DATA_BYTES = {payload["packet_data_bytes"]};
inline constexpr std::size_t PAYLOAD_CACHE_MAX_BYTES = {payload["cache_max_bytes"]};
inline constexpr std::uint16_t PAYLOAD_CACHE_CHUNK_BYTES = {payload["cache_chunk_bytes"]};
inline constexpr std::size_t PREVIEW_MAX_FRAME_BYTES = {preview["max_frame_bytes"]};
inline constexpr std::uint16_t PREVIEW_CHUNK_BYTES = {preview["chunk_bytes"]};
inline constexpr std::uint8_t PREVIEW_WIDTH = {preview["width"]};
inline constexpr std::uint8_t PREVIEW_HEIGHT = {preview["height"]};
inline constexpr std::uint8_t PREVIEW_PIXEL_FORMAT_U8 = {preview["pixel_format_u8"]};
inline constexpr std::uint8_t PREVIEW_OP_BEGIN = {preview["op_begin"]};
inline constexpr std::uint8_t PREVIEW_OP_CHUNK = {preview["op_chunk"]};
inline constexpr std::uint8_t PREVIEW_OP_COMMIT_AND_SEND = {preview["op_commit_and_send"]};
inline constexpr std::uint8_t PREVIEW_OP_ABORT = {preview["op_abort"]};
inline constexpr std::uint8_t PREVIEW_STATE_EMPTY = {preview["state_empty"]};
inline constexpr std::uint8_t PREVIEW_STATE_RECEIVING = {preview["state_receiving"]};
inline constexpr std::uint8_t PREVIEW_STATE_READY = {preview["state_ready"]};
inline constexpr std::uint8_t PREVIEW_STATE_SENDING = {preview["state_sending"]};
inline constexpr std::uint8_t PREVIEW_STATE_ERROR = {preview["state_error"]};
inline constexpr std::uint8_t PREVIEW_WIRE_MAGIC_0 = {hex_byte(preview["wire_magic_0"])};
inline constexpr std::uint8_t PREVIEW_WIRE_MAGIC_1 = {hex_byte(preview["wire_magic_1"])};
inline constexpr std::uint8_t PREVIEW_WIRE_VERSION = {preview["wire_version"]};
inline constexpr std::uint8_t PREVIEW_WIRE_TYPE_FRAGMENT = {preview["wire_type_fragment"]};
}}  // namespace Generated
}}  // namespace SatelliteController

#endif
"""


def render_all(cfg: dict, registry: dict) -> dict[Path, str]:
    identity = resolve_rf_identity(cfg, registry)
    return {
        OUTPUTS["fprime"]: render_fprime(cfg, identity),
        OUTPUTS["satellite"]: render_teensy(cfg, identity, satellite=True),
        OUTPUTS["ground"]: render_teensy(cfg, identity, satellite=False),
        OUTPUTS["zephyr"]: render_zephyr(cfg, identity),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if generated headers are not up to date")
    args = parser.parse_args()

    cfg = json.loads(MANIFEST.read_text())
    registry = json.loads(RF_NETWORKS.read_text())
    generated = render_all(cfg, registry)

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
