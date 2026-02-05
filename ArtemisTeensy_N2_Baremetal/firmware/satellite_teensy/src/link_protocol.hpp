#ifndef ARTEMIS_TEENSY_LINK_PROTOCOL_HPP
#define ARTEMIS_TEENSY_LINK_PROTOCOL_HPP

#include <Arduino.h>

namespace link_protocol {

static constexpr uint8_t FRAME_MAGIC_0 = 0xD4;
static constexpr uint8_t FRAME_MAGIC_1 = 0xC3;
static constexpr uint16_t FRAME_MAX_PAYLOAD = 49;  // RF23BP max payload in this project
static constexpr uint32_t FRAME_TIMEOUT_MS = 250;

static constexpr char COMMAND_PREFIX = '#';
static constexpr size_t COMMAND_MAX_LEN = 64;

static constexpr const char* CMD_PING = "PING";
static constexpr const char* CMD_LINK_STATUS = "LINK_STATUS";
static constexpr const char* CMD_RESET_COUNTERS = "RESET_COUNTERS";

static constexpr const char* RESP_PONG = "#PONG\n";
static constexpr const char* RESP_RESET_OK = "#OK RESET_COUNTERS\n";

}  // namespace link_protocol

#endif
