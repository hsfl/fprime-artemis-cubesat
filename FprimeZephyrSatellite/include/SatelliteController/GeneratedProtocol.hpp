#ifndef ARTEMIS_FPRIME_ZEPHYR_GENERATED_PROTOCOL_HPP
#define ARTEMIS_FPRIME_ZEPHYR_GENERATED_PROTOCOL_HPP

// Generated from config/transport_constants.json and config/rf_networks.json by tools/generate_transport_constants.py.
// Do not hand-edit constants here; update the manifest and regenerate.

#include <cstddef>
#include <cstdint>

namespace SatelliteController {
namespace Generated {
inline constexpr std::uint8_t FRAME_MAGIC_0 = 0xD4;
inline constexpr std::uint8_t FRAME_MAGIC_1 = 0xC3;
inline constexpr std::uint8_t CHANNEL_CCSDS = 0;
inline constexpr std::uint8_t CHANNEL_PAYLOAD = 1;
inline constexpr std::uint8_t CHANNEL_LOCAL = 2;
inline constexpr std::size_t CHANNEL_COUNT = 3;
inline constexpr std::size_t RF_CHANNEL_COUNT = 2;
inline constexpr std::size_t FRAME_MAX_PAYLOAD = 220;
inline constexpr std::uint32_t FRAME_TIMEOUT_MS = 250;
inline constexpr std::uint32_t UART_BAUD = 115200;
inline constexpr std::uint8_t RF_NETWORK_ID = 0xC3;
inline constexpr std::uint8_t RF_PROTOCOL_VERSION = 0x01;
inline constexpr std::uint8_t RF_LOCAL_ADDRESS = 0xA2;
inline constexpr std::uint8_t RF_REMOTE_ADDRESS = 0xA1;
inline constexpr std::uint8_t RF_MAGIC_CCSDS = 0xA5;
inline constexpr std::uint8_t RF_MAGIC_PAYLOAD = 0xA6;
inline constexpr std::uint8_t RF_ACK_INDEX = 0xFF;
inline constexpr std::size_t RF_PACKET_MAX_LEN = 49;
inline constexpr std::size_t RF_SEGMENT_HEADER_LEN = 5;
inline constexpr std::size_t RF_SEGMENT_MAX_DATA = RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
inline constexpr std::uint32_t RF_REASSEMBLY_TIMEOUT_MS = 500;
inline constexpr std::uint32_t RF_INTER_SEGMENT_GAP_MS = 8;
inline constexpr std::uint32_t RF_PAYLOAD_INTER_PACKET_GAP_MS = 15;
inline constexpr std::uint32_t RF_TX_COMPLETE_TIMEOUT_MS = 500;
inline constexpr std::uint8_t RF_ACK_RETRIES = 4;
inline constexpr std::uint32_t RF_ACK_TIMEOUT_MS = 80;
inline constexpr bool TX_ACK_REQUIRED_CCSDS = false;
inline constexpr bool TX_ACK_REQUIRED_PAYLOAD = false;
inline constexpr bool RX_ACK_REQUIRED_CCSDS = true;
inline constexpr bool RX_ACK_REQUIRED_PAYLOAD = false;
inline constexpr std::uint8_t TEENSY_TARGET_PDU = 1;
inline constexpr std::uint8_t TEENSY_TARGET_RF_STATUS = 2;
inline constexpr std::uint8_t TEENSY_TARGET_PAYLOAD_CACHE = 3;
inline constexpr std::uint8_t TEENSY_TARGET_LEPTON_PREVIEW = 4;
inline constexpr std::uint8_t TEENSY_STATUS_OK = 0;
inline constexpr std::uint8_t TEENSY_STATUS_BAD_REQUEST = 1;
inline constexpr std::uint8_t TEENSY_STATUS_BUSY = 2;
inline constexpr std::uint8_t TEENSY_STATUS_TIMEOUT = 3;
inline constexpr std::uint8_t TEENSY_STATUS_TARGET_ERROR = 4;
inline constexpr std::uint8_t TEENSY_RF_OP_STATUS = 1;
inline constexpr std::uint8_t TEENSY_RF_OP_SET_ENABLED = 2;
inline constexpr std::uint32_t TEENSY_RF_RSSI_AGE_UNKNOWN_MS = 4294967295;
inline constexpr std::uint8_t PAYLOAD_CACHE_OP_BEGIN = 1;
inline constexpr std::uint8_t PAYLOAD_CACHE_OP_CHUNK = 2;
inline constexpr std::uint8_t PAYLOAD_CACHE_OP_COMMIT_AND_SEND = 3;
inline constexpr std::uint8_t PAYLOAD_CACHE_OP_ABORT = 4;
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_EMPTY = 0;
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_RECEIVING = 1;
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_READY = 2;
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_SENDING = 3;
inline constexpr std::uint8_t PAYLOAD_CACHE_STATE_ERROR = 4;
inline constexpr std::uint8_t PAYLOAD_MAGIC_0 = 0x4E;
inline constexpr std::uint8_t PAYLOAD_MAGIC_1 = 0x32;
inline constexpr std::uint8_t PAYLOAD_PACKET_DATA_BYTES = 35;
inline constexpr std::size_t PAYLOAD_CACHE_MAX_BYTES = 196608;
inline constexpr std::uint16_t PAYLOAD_CACHE_CHUNK_BYTES = 200;
inline constexpr std::size_t PREVIEW_MAX_FRAME_BYTES = 4800;
inline constexpr std::uint16_t PREVIEW_CHUNK_BYTES = 200;
inline constexpr std::uint8_t PREVIEW_WIDTH = 80;
inline constexpr std::uint8_t PREVIEW_HEIGHT = 60;
inline constexpr std::uint8_t PREVIEW_PIXEL_FORMAT_U8 = 1;
inline constexpr std::uint8_t PREVIEW_OP_BEGIN = 1;
inline constexpr std::uint8_t PREVIEW_OP_CHUNK = 2;
inline constexpr std::uint8_t PREVIEW_OP_COMMIT_AND_SEND = 3;
inline constexpr std::uint8_t PREVIEW_OP_ABORT = 4;
inline constexpr std::uint8_t PREVIEW_STATE_EMPTY = 0;
inline constexpr std::uint8_t PREVIEW_STATE_RECEIVING = 1;
inline constexpr std::uint8_t PREVIEW_STATE_READY = 2;
inline constexpr std::uint8_t PREVIEW_STATE_SENDING = 3;
inline constexpr std::uint8_t PREVIEW_STATE_ERROR = 4;
inline constexpr std::uint8_t PREVIEW_WIRE_MAGIC_0 = 0x50;
inline constexpr std::uint8_t PREVIEW_WIRE_MAGIC_1 = 0x56;
inline constexpr std::uint8_t PREVIEW_WIRE_VERSION = 1;
inline constexpr std::uint8_t PREVIEW_WIRE_TYPE_FRAGMENT = 1;
}  // namespace Generated
}  // namespace SatelliteController

#endif
