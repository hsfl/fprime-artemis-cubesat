#ifndef ARTEMIS_TEENSY_LINK_PROTOCOL_HPP
#define ARTEMIS_TEENSY_LINK_PROTOCOL_HPP

#include <Arduino.h>

namespace link_protocol {

static constexpr uint8_t FRAME_MAGIC_0 = 0xD4;
static constexpr uint8_t FRAME_MAGIC_1 = 0xC3;

// UART wrapper payload carries opaque F' bytes.
static constexpr uint16_t FRAME_MAX_PAYLOAD = 220;
static constexpr uint32_t FRAME_TIMEOUT_MS = 250;

// RF segmentation parameters.
static constexpr uint8_t RF_SEGMENT_MAGIC = 0xA5;
static constexpr uint8_t RF_PACKET_MAX_LEN = 49;
static constexpr uint8_t RF_SEGMENT_HEADER_LEN = 5;
static constexpr uint8_t RF_SEGMENT_MAX_DATA = RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
static constexpr uint32_t RF_REASSEMBLY_TIMEOUT_MS = 500;

static constexpr char COMMAND_PREFIX = '#';
static constexpr size_t COMMAND_MAX_LEN = 64;

static constexpr const char* CMD_PING = "PING";
static constexpr const char* CMD_LINK_STATUS = "LINK_STATUS";
static constexpr const char* CMD_RESET_COUNTERS = "RESET_COUNTERS";

static constexpr const char* RESP_PONG = "#PONG\n";
static constexpr const char* RESP_RESET_OK = "#OK RESET_COUNTERS\n";

}  // namespace link_protocol

#endif
