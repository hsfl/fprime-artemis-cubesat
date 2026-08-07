#!/usr/bin/env python3
"""Generate F Prime and Teensy transport-constant headers from one manifest."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "config/transport_constants.json"
RF_NETWORKS = ROOT / "config/rf_networks.json"
COMCFG_TEMPLATE = (
    ROOT / "ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig/ComCfg.fpp.in"
)
COMCFG_OUTPUT_DIR = ROOT / "ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig"

OUTPUTS = {
    "fprime": ROOT / "ArtemisRpiTeensy_N2/Components/LinkCfg/LinkCfg.hpp",
    "satellite": ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp",
    "ground": ROOT / "GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp",
}

PROFILE_NAME_RE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")


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


def profile_macro(profile_key: str) -> str:
    return "RF_PROFILE_" + profile_key.upper().replace("-", "_")


def validate_rf_registry(registry: dict) -> None:
    networks = registry.get("networks", {})
    if not networks:
        raise ValueError("RF network registry must define at least one network")
    ids = [entry.get("id") for entry in networks.values()]
    if any(not isinstance(value, int) or not 1 <= value <= 254 for value in ids):
        raise ValueError("RF network IDs must be unique integers in the range 1..254")
    if len(ids) != len(set(ids)):
        raise ValueError("RF network IDs must be unique")
    version = registry.get("protocol_version")
    if not isinstance(version, int) or not 1 <= version <= 255:
        raise ValueError("RF protocol version must be an integer in the range 1..255")

    profiles = registry.get("endpoint_profiles", {})
    if not profiles:
        raise ValueError("RF network registry must define endpoint_profiles")

    local_addresses: set[tuple[str, int]] = set()
    spacecraft_ids: set[int] = set()
    payload_namespaces: set[str] = set()
    gds_sessions: set[str] = set()
    for profile_key, endpoint in profiles.items():
        if not PROFILE_NAME_RE.fullmatch(profile_key):
            raise ValueError(f"invalid RF endpoint profile name {profile_key!r}")
        network_key = endpoint.get("network")
        if network_key not in networks:
            raise ValueError(
                f"RF endpoint profile {profile_key!r} uses unknown network {network_key!r}"
            )
        role = endpoint.get("role")
        if role not in ("ground", "spacecraft"):
            raise ValueError(
                f"RF endpoint profile {profile_key!r} role must be 'ground' or 'spacecraft'"
            )
        local_address = endpoint.get("local_address")
        if not isinstance(local_address, int) or not 1 <= local_address <= 254:
            raise ValueError(
                f"RF endpoint profile {profile_key!r} local_address must be in the range 1..254"
            )
        address_key = (network_key, local_address)
        if address_key in local_addresses:
            raise ValueError(
                f"RF endpoint local addresses must be unique within network {network_key!r}"
            )
        local_addresses.add(address_key)

        spacecraft_id = endpoint.get("ccsds_spacecraft_id")
        if spacecraft_id is not None:
            if role != "spacecraft" or not isinstance(spacecraft_id, int) or not 0 <= spacecraft_id <= 0x03FF:
                raise ValueError(
                    f"RF endpoint profile {profile_key!r} has an invalid CCSDS spacecraft ID"
                )
            if spacecraft_id in spacecraft_ids:
                raise ValueError("CCSDS spacecraft IDs must be unique")
            spacecraft_ids.add(spacecraft_id)

        payload_namespace = endpoint.get("payload_namespace")
        if payload_namespace is not None:
            if role != "spacecraft" or not PROFILE_NAME_RE.fullmatch(payload_namespace):
                raise ValueError(
                    f"RF endpoint profile {profile_key!r} has an invalid payload namespace"
                )
            if payload_namespace in payload_namespaces:
                raise ValueError("payload namespaces must be unique")
            payload_namespaces.add(payload_namespace)

        gds_session = endpoint.get("gds_session")
        if gds_session is not None:
            if role != "ground" or not PROFILE_NAME_RE.fullmatch(gds_session):
                raise ValueError(
                    f"RF endpoint profile {profile_key!r} has an invalid GDS session"
                )
            if gds_session in gds_sessions:
                raise ValueError("GDS sessions must be unique")
            gds_sessions.add(gds_session)

    for profile_key, endpoint in profiles.items():
        remote_key = endpoint.get("remote_profile")
        if remote_key not in profiles:
            raise ValueError(
                f"RF endpoint profile {profile_key!r} uses unknown remote_profile {remote_key!r}"
            )
        remote = profiles[remote_key]
        if remote.get("remote_profile") != profile_key:
            raise ValueError(
                f"RF endpoint profiles {profile_key!r} and {remote_key!r} must pair reciprocally"
            )
        if remote.get("network") != endpoint.get("network"):
            raise ValueError(
                f"RF endpoint profiles {profile_key!r} and {remote_key!r} must use the same network"
            )
        if remote.get("role") == endpoint.get("role"):
            raise ValueError(
                f"RF endpoint profiles {profile_key!r} and {remote_key!r} must use opposite roles"
            )


def resolve_endpoint_profile(
    registry: dict, profile_key: str, *, expected_role: str = None
) -> dict:
    validate_rf_registry(registry)
    profiles = registry["endpoint_profiles"]
    if profile_key not in profiles:
        raise ValueError(
            f"RF endpoint profile {profile_key!r} is not defined in config/rf_networks.json"
        )
    endpoint = profiles[profile_key]
    role = endpoint["role"]
    if expected_role is not None and role != expected_role:
        raise ValueError(
            f"RF endpoint profile {profile_key!r} has role {role!r}, expected {expected_role!r}"
        )
    remote_key = endpoint["remote_profile"]
    remote = profiles[remote_key]
    network_key = endpoint["network"]
    return {
        "profile_key": profile_key,
        "profile_macro": profile_macro(profile_key),
        "role": role,
        "network_key": network_key,
        "network_id": registry["networks"][network_key]["id"],
        "protocol_version": registry["protocol_version"],
        "local_address": endpoint["local_address"],
        "remote_profile": remote_key,
        "remote_address": remote["local_address"],
        "ccsds_spacecraft_id": endpoint.get("ccsds_spacecraft_id"),
        "payload_namespace": endpoint.get("payload_namespace"),
        "gds_session": endpoint.get("gds_session"),
        "gds_gui_port": endpoint.get("gds_gui_port"),
    }


def resolve_rf_identity(cfg: dict, registry: dict) -> dict:
    selections = cfg["rf"].get("endpoint_profiles", {})
    ground_key = selections.get("ground")
    spacecraft_key = selections.get("spacecraft")
    ground = resolve_endpoint_profile(registry, ground_key, expected_role="ground")
    spacecraft = resolve_endpoint_profile(registry, spacecraft_key, expected_role="spacecraft")
    if ground["remote_profile"] != spacecraft_key or spacecraft["remote_profile"] != ground_key:
        raise ValueError("selected RF ground and spacecraft endpoint profiles must be a paired tuple")
    return {
        "network_key": ground["network_key"],
        "network_id": ground["network_id"],
        "protocol_version": ground["protocol_version"],
        "ground_profile": ground_key,
        "spacecraft_profile": spacecraft_key,
        "ground_address": ground["local_address"],
        "satellite_address": spacecraft["local_address"],
    }


def render_fprime(cfg: dict, identity: dict) -> str:
    frame = cfg["frame"]
    channels = cfg["channels"]
    rpc = cfg["teensy_rpc"]
    rf = cfg["rf"]
    ground_ack = rf["ack_directions"]["ground_to_satellite"]
    satellite_ack = rf["ack_directions"]["satellite_to_ground"]
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
static constexpr U16 RF_TX_COMPLETE_TIMEOUT_MS = {rf["tx_complete_timeout_ms"]};
static constexpr U8 RF_GROUND_TX_ACK_REQUIRED_CCSDS = {cpp_flag(ground_ack["ccsds"])};
static constexpr U8 RF_GROUND_TX_ACK_REQUIRED_PAYLOAD = {cpp_flag(ground_ack["payload"])};
static constexpr U8 RF_SATELLITE_TX_ACK_REQUIRED_CCSDS = {cpp_flag(satellite_ack["ccsds"])};
static constexpr U8 RF_SATELLITE_TX_ACK_REQUIRED_PAYLOAD = {cpp_flag(satellite_ack["payload"])};
static constexpr FwSizeType PAYLOAD_PACKET_MAX_BYTES = RF_SEGMENT_MAX_DATA_BYTES;
static constexpr FwSizeType PAYLOAD_PACKET_DATA_BYTES = {payload["packet_data_bytes"]};
static constexpr U8 PAYLOAD_MAGIC_0 = {hex_byte(payload["magic_0"])};  // 'N'
static constexpr U8 PAYLOAD_MAGIC_1 = {hex_byte(payload["magic_1"])};  // '2'

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


def render_profile_selector(registry: dict, *, role: str, default_profile: str) -> str:
    endpoints = [
        resolve_endpoint_profile(registry, key, expected_role=role)
        for key, value in registry["endpoint_profiles"].items()
        if value["role"] == role
    ]
    macro_lines = [
        f"#define {profile_macro(key)} {index}"
        for index, key in enumerate(registry["endpoint_profiles"], start=1)
    ]
    branches = []
    for index, endpoint in enumerate(endpoints):
        directive = "#if" if index == 0 else "#elif"
        branches.append(
            f"""{directive} RF_ENDPOINT_PROFILE == {endpoint['profile_macro']}
#define RF_ENDPOINT_PROFILE_NAME {cpp_string(endpoint['profile_key'])}
static constexpr uint8_t RF_NETWORK_ID = {hex_byte(endpoint['network_id'])};
static constexpr uint8_t RF_PROTOCOL_VERSION = {hex_byte(endpoint['protocol_version'])};
static constexpr uint8_t RF_LOCAL_ADDRESS = {hex_byte(endpoint['local_address'])};
static constexpr uint8_t RF_REMOTE_ADDRESS = {hex_byte(endpoint['remote_address'])};"""
        )
    role_label = "ground/GDS" if role == "ground" else "spacecraft"
    return f"""// Named RF endpoint profiles. Select one with compiler.cpp.extra_flags,
// for example -DRF_ENDPOINT_PROFILE={profile_macro(default_profile)}.
{chr(10).join(macro_lines)}

#ifndef RF_ENDPOINT_PROFILE
#define RF_ENDPOINT_PROFILE {profile_macro(default_profile)}
#endif

{chr(10).join(branches)}
#else
#error "Unknown or wrong-role RF_ENDPOINT_PROFILE for this {role_label} firmware"
#endif"""


def render_teensy(cfg: dict, registry: dict, *, satellite: bool) -> str:
    frame = cfg["frame"]
    channels = cfg["channels"]
    rpc = cfg["teensy_rpc"]
    rf = cfg["rf"]
    ground_ack = rf["ack_directions"]["ground_to_satellite"]
    satellite_ack = rf["ack_directions"]["satellite_to_ground"]
    tx_ack = satellite_ack if satellite else ground_ack
    rx_ack = ground_ack if satellite else satellite_ack
    command = cfg["command"]
    count = channels["satellite_count"] if satellite else channels["ground_count"]
    role = "spacecraft" if satellite else "ground"
    default_profile = cfg["rf"]["endpoint_profiles"][role]
    profile_selector = render_profile_selector(registry, role=role, default_profile=default_profile)
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

// RF segmentation parameters.
{profile_selector}
static constexpr uint8_t RF_SEGMENT_MAGIC_CCSDS = {hex_byte(rf["segment_magic_ccsds"])};
static constexpr uint8_t RF_SEGMENT_MAGIC_PAYLOAD = {hex_byte(rf["segment_magic_payload"])};
static constexpr uint8_t RF_ACK_SEGMENT_INDEX = {hex_byte(rf["ack_segment_index"])};
static constexpr uint8_t RF_PACKET_MAX_LEN = {rf["packet_max_len"]};
static constexpr uint8_t RF_SEGMENT_HEADER_LEN = {rf["segment_header_len"]};
static constexpr uint8_t RF_SEGMENT_MAX_DATA = RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
static constexpr uint32_t RF_REASSEMBLY_TIMEOUT_MS = {rf["reassembly_timeout_ms"]};
static constexpr uint8_t RF_INTER_SEGMENT_GAP_MS = {rf["inter_segment_gap_ms"]};
static constexpr uint16_t RF_TX_COMPLETE_TIMEOUT_MS = {rf["tx_complete_timeout_ms"]};
static constexpr uint8_t RF_ACK_RETRIES = {rf["ack_retries"]};
static constexpr uint16_t RF_ACK_TIMEOUT_MS = {rf["ack_timeout_ms"]};
static constexpr uint8_t RF_TX_ACK_REQUIRED_CCSDS = {cpp_flag(tx_ack["ccsds"])};
static constexpr uint8_t RF_TX_ACK_REQUIRED_PAYLOAD = {cpp_flag(tx_ack["payload"])};
static constexpr uint8_t RF_RX_ACK_REQUIRED_CCSDS = {cpp_flag(rx_ack["ccsds"])};
static constexpr uint8_t RF_RX_ACK_REQUIRED_PAYLOAD = {cpp_flag(rx_ack["payload"])};

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


def render_comcfg(template: str, endpoint: dict) -> str:
    spacecraft_id = endpoint["ccsds_spacecraft_id"]
    if spacecraft_id is None:
        raise ValueError(
            f"spacecraft profile {endpoint['profile_key']!r} must define ccsds_spacecraft_id"
        )
    notice = (
        "# Generated from config/rf_networks.json and ComCfg.fpp.in by "
        "tools/generate_transport_constants.py.\n"
        "# Do not hand-edit; update the registry/template and regenerate.\n\n"
    )
    return notice + template.replace("@SPACECRAFT_ID@", f"0x{spacecraft_id:04X}")


def render_all(cfg: dict, registry: dict) -> dict[Path, str]:
    identity = resolve_rf_identity(cfg, registry)
    generated = {
        OUTPUTS["fprime"]: render_fprime(cfg, identity),
        OUTPUTS["satellite"]: render_teensy(cfg, registry, satellite=True),
        OUTPUTS["ground"]: render_teensy(cfg, registry, satellite=False),
    }
    template = COMCFG_TEMPLATE.read_text()
    for profile_key, value in registry["endpoint_profiles"].items():
        if value["role"] != "spacecraft" or value.get("ccsds_spacecraft_id") is None:
            continue
        endpoint = resolve_endpoint_profile(registry, profile_key, expected_role="spacecraft")
        path = COMCFG_OUTPUT_DIR / f"ComCfg.{profile_key}.fpp"
        generated[path] = render_comcfg(template, endpoint)
    return generated


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
