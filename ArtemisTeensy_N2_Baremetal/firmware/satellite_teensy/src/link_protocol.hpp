#ifndef ARTEMIS_TEENSY_LINK_PROTOCOL_HPP
#define ARTEMIS_TEENSY_LINK_PROTOCOL_HPP

// Generated from config/transport_constants.json by tools/generate_transport_constants.py.
// Do not hand-edit constants here; update the manifest and regenerate.

#include <Arduino.h>

namespace link_protocol {

static constexpr uint8_t FRAME_MAGIC_0 = 0xD4;
static constexpr uint8_t FRAME_MAGIC_1 = 0xC3;
static constexpr uint8_t CHANNEL_CCSDS = 0;
static constexpr uint8_t CHANNEL_PAYLOAD = 1;
static constexpr uint8_t CHANNEL_TEENSY_LOCAL = 2;
static constexpr uint8_t CHANNEL_RF_COUNT = 2;
static constexpr uint8_t CHANNEL_COUNT = 3;

static constexpr uint8_t TEENSY_TARGET_PDU = 1;
static constexpr uint8_t TEENSY_TARGET_RF_STATUS = 2;
static constexpr uint8_t TEENSY_STATUS_OK = 0;
static constexpr uint8_t TEENSY_STATUS_BAD_REQUEST = 1;
static constexpr uint8_t TEENSY_STATUS_BUSY = 2;
static constexpr uint8_t TEENSY_STATUS_TIMEOUT = 3;
static constexpr uint8_t TEENSY_STATUS_TARGET_ERROR = 4;
static constexpr uint8_t TEENSY_RF_OP_LINK_STATS = 1;

// UART wrapper payload carries opaque bytes tagged by virtual channel.
static constexpr uint16_t FRAME_MAX_PAYLOAD = 220;
static constexpr uint32_t FRAME_TIMEOUT_MS = 250;

// RF segmentation parameters.
static constexpr uint8_t RF_SEGMENT_MAGIC_CCSDS = 0xA5;
static constexpr uint8_t RF_SEGMENT_MAGIC_PAYLOAD = 0xA6;
static constexpr uint8_t RF_ACK_SEGMENT_INDEX = 0xFF;
static constexpr uint8_t RF_PACKET_MAX_LEN = 49;
static constexpr uint8_t RF_SEGMENT_HEADER_LEN = 5;
static constexpr uint8_t RF_SEGMENT_MAX_DATA = RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
static constexpr uint32_t RF_REASSEMBLY_TIMEOUT_MS = 500;
static constexpr uint8_t RF_INTER_SEGMENT_GAP_MS = 8;
static constexpr uint8_t RF_ACK_RETRIES = 4;
static constexpr uint16_t RF_ACK_TIMEOUT_MS = 80;
static constexpr uint8_t RF_ACK_REQUIRED_CCSDS = 1;
static constexpr uint8_t RF_ACK_REQUIRED_PAYLOAD = 0;

static constexpr uint32_t PAYLOAD_PACKETS_PER_RUN = 32;
static constexpr uint32_t PAYLOAD_RETRY_PACKETS_PER_RUN = 32;

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

inline bool ackRequiredForChannel(uint8_t channel) {
  return channel == CHANNEL_PAYLOAD
             ? RF_ACK_REQUIRED_PAYLOAD != 0
             : RF_ACK_REQUIRED_CCSDS != 0;
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
