#ifndef ARTEMIS_TEENSY_LINK_PROTOCOL_HPP
#define ARTEMIS_TEENSY_LINK_PROTOCOL_HPP

// Generated from config/transport_constants.json and config/rf_networks.json by tools/generate_transport_constants.py.
// Do not hand-edit constants here; update the manifest and regenerate.

#include <Arduino.h>

namespace link_protocol {

static constexpr uint8_t FRAME_MAGIC_0 = 0xD4;
static constexpr uint8_t FRAME_MAGIC_1 = 0xC3;
static constexpr uint8_t CHANNEL_CCSDS = 0;
static constexpr uint8_t CHANNEL_PAYLOAD = 1;
static constexpr uint8_t CHANNEL_RF_COUNT = 2;
static constexpr uint8_t CHANNEL_COUNT = 2;

// UART wrapper payload carries opaque bytes tagged by virtual channel.
static constexpr uint16_t FRAME_MAX_PAYLOAD = 220;
static constexpr uint32_t FRAME_TIMEOUT_MS = 250;
static constexpr uint32_t UART_BAUD = 115200;
static constexpr uint32_t UART_INTER_FRAME_MARGIN_US = 37000;
static constexpr uint32_t UART_PAYLOAD_CACHE_MARGIN_US = 1000;
static constexpr uint32_t UART_CCSDS_EXTRA_MARGIN_US = 40000;

// RF segmentation parameters.
static constexpr uint8_t RF_NETWORK_ID = 0xC3;
static constexpr uint8_t RF_PROTOCOL_VERSION = 0x01;
static constexpr uint8_t RF_LOCAL_ADDRESS = 0xA1;
static constexpr uint8_t RF_REMOTE_ADDRESS = 0xA2;
static constexpr uint8_t RF_SEGMENT_MAGIC_CCSDS = 0xA5;
static constexpr uint8_t RF_SEGMENT_MAGIC_PAYLOAD = 0xA6;
static constexpr uint8_t RF_ACK_SEGMENT_INDEX = 0xFF;
static constexpr uint8_t RF_PACKET_MAX_LEN = 49;
static constexpr uint8_t RF_SEGMENT_HEADER_LEN = 5;
static constexpr uint8_t RF_SEGMENT_MAX_DATA = RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
static constexpr uint32_t RF_REASSEMBLY_TIMEOUT_MS = 500;
static constexpr uint8_t RF_INTER_SEGMENT_GAP_MS = 8;
static constexpr uint8_t RF_PAYLOAD_INTER_PACKET_GAP_MS = 15;
static constexpr uint16_t RF_TX_COMPLETE_TIMEOUT_MS = 500;
static constexpr uint8_t RF_ACK_RETRIES = 4;
static constexpr uint16_t RF_ACK_TIMEOUT_MS = 80;
static constexpr uint8_t RF_TX_ACK_REQUIRED_CCSDS = 1;
static constexpr uint8_t RF_TX_ACK_REQUIRED_PAYLOAD = 0;
static constexpr uint8_t RF_RX_ACK_REQUIRED_CCSDS = 0;
static constexpr uint8_t RF_RX_ACK_REQUIRED_PAYLOAD = 0;

static constexpr uint32_t PAYLOAD_PACKETS_PER_RUN = 18;
static constexpr uint32_t PAYLOAD_RETRY_PACKETS_PER_RUN = 18;
static constexpr uint8_t PAYLOAD_MAGIC_0 = 0x4E;
static constexpr uint8_t PAYLOAD_MAGIC_1 = 0x32;
static constexpr uint8_t PAYLOAD_PACKET_DATA_BYTES = 35;
static constexpr uint32_t PAYLOAD_CACHE_MAX_BYTES = 196608;
static constexpr uint16_t PAYLOAD_CACHE_CHUNK_BYTES = 200;
static constexpr uint8_t PAYLOAD_CACHE_RF_GAP_MS = 2;
static constexpr uint8_t PAYLOAD_CACHE_OP_BEGIN = 1;
static constexpr uint8_t PAYLOAD_CACHE_OP_CHUNK = 2;
static constexpr uint8_t PAYLOAD_CACHE_OP_COMMIT_AND_SEND = 3;
static constexpr uint8_t PAYLOAD_CACHE_OP_ABORT = 4;
static constexpr uint8_t PAYLOAD_CACHE_STATE_EMPTY = 0;
static constexpr uint8_t PAYLOAD_CACHE_STATE_RECEIVING = 1;
static constexpr uint8_t PAYLOAD_CACHE_STATE_READY = 2;
static constexpr uint8_t PAYLOAD_CACHE_STATE_SENDING = 3;
static constexpr uint8_t PAYLOAD_CACHE_STATE_ERROR = 4;

static constexpr char COMMAND_PREFIX = '#';
static constexpr size_t COMMAND_MAX_LEN = 64;

static constexpr const char* CMD_PING = "PING";
static constexpr const char* CMD_LINK_STATUS = "LINK_STATUS";
static constexpr const char* CMD_RESET_COUNTERS = "RESET_COUNTERS";

static constexpr const char* RESP_PONG = "#PONG\n";
static constexpr const char* RESP_RESET_OK = "#OK RESET_COUNTERS\n";

inline bool isValidChannel(uint8_t channel) {
  return channel < CHANNEL_COUNT;
}

inline bool isRfChannel(uint8_t channel) {
  return channel < CHANNEL_RF_COUNT;
}

enum class RfHeaderStatus : uint8_t {
  ACCEPT = 0,
  WRONG_NETWORK = 1,
  WRONG_ADDRESS = 2,
  WRONG_VERSION = 3,
};

inline RfHeaderStatus classifyRfHeader(uint8_t to, uint8_t from, uint8_t id, uint8_t flags) {
  if (id != RF_NETWORK_ID) {
    return RfHeaderStatus::WRONG_NETWORK;
  }
  if (to != RF_LOCAL_ADDRESS || from != RF_REMOTE_ADDRESS) {
    return RfHeaderStatus::WRONG_ADDRESS;
  }
  if (flags != RF_PROTOCOL_VERSION) {
    return RfHeaderStatus::WRONG_VERSION;
  }
  return RfHeaderStatus::ACCEPT;
}

inline bool txAckRequiredForChannel(uint8_t channel) {
  return channel == CHANNEL_PAYLOAD
             ? RF_TX_ACK_REQUIRED_PAYLOAD != 0
             : RF_TX_ACK_REQUIRED_CCSDS != 0;
}

inline bool rxAckRequiredForChannel(uint8_t channel) {
  return channel == CHANNEL_PAYLOAD
             ? RF_RX_ACK_REQUIRED_PAYLOAD != 0
             : RF_RX_ACK_REQUIRED_CCSDS != 0;
}

inline uint8_t magicForChannel(uint8_t channel) {
  return channel == CHANNEL_PAYLOAD ? RF_SEGMENT_MAGIC_PAYLOAD : RF_SEGMENT_MAGIC_CCSDS;
}

inline bool channelForMagic(uint8_t magic, uint8_t& channel) {
  if (magic == RF_SEGMENT_MAGIC_CCSDS) {
    channel = CHANNEL_CCSDS;
    return true;
  }
  if (magic == RF_SEGMENT_MAGIC_PAYLOAD) {
    channel = CHANNEL_PAYLOAD;
    return true;
  }
  return false;
}

}  // namespace link_protocol

#endif
