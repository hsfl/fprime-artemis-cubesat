#ifndef ARTEMIS_TEENSY_RELAY_UART_RF_HPP
#define ARTEMIS_TEENSY_RELAY_UART_RF_HPP

#include <Arduino.h>

#include "link_counters.hpp"
#include "link_protocol.hpp"
#include "rf23_driver.hpp"

struct RelayConfig {
  bool enableUartToRf = true;
  bool uartOutputFramed = true;
  bool enableCommandMode = true;
  bool uartInputFramed = true;
  uint16_t rawUartFlushMs = 8;
  uint8_t uplinkQueueDepth = 16;
  uint8_t downlinkQueueDepth = 16;
  uint16_t rawUartChunkBytes = link_protocol::RF_SEGMENT_MAX_DATA;
};

class RelayUartRf {
 public:
  RelayUartRf(Stream& linkIo,
              Rf23Driver& rfDriver,
              LinkCounters& counters,
              const RelayConfig& config = RelayConfig{});

  void begin();
  void poll();

 private:
  enum class ParseState {
    WAIT_MAGIC_0,
    WAIT_MAGIC_1,
    WAIT_LEN_LO,
    WAIT_LEN_HI,
    WAIT_PAYLOAD,
    WAIT_CRC_LO,
    WAIT_CRC_HI
  };

  void processUartByte(uint8_t b);
  bool handleHandshakeByte(uint8_t b);
  void processRawUartByte(uint8_t b);
  void flushRawUartIfStale();
  void processCommandByte(uint8_t b);
  void processFrameByte(uint8_t b);
  void flushRfToUart();

  void resetFrameParser(bool timeoutReset);
  void handleCompletedFrame();
  bool sendUartFrame(const uint8_t* payload, uint16_t length);
  bool sendRawToUart(const uint8_t* payload, uint16_t length);

  bool sendPayloadOverRf(const uint8_t* payload, uint16_t length);
  bool sendRfPacketWithAck(const uint8_t* packet, uint8_t packetLen, uint8_t msgId, uint8_t segIdx);
  bool waitForAck(uint8_t msgId, uint8_t segIdx);
  bool isAckPacket(const uint8_t* packet, uint8_t packetLen, uint8_t msgId, uint8_t segIdx) const;
  bool sendAck(uint8_t msgId, uint8_t segIdx);
  void processRfSegment(const uint8_t* packet, uint8_t packetLen);
  void resetReassembly(bool timeoutReset, bool dropReset);

  uint16_t crc16Ccitt(const uint8_t* data, uint16_t len) const;
  void emitLinkStatus();
  bool enqueueUplinkMessage(const uint8_t* payload, uint16_t length);
  bool enqueueDownlinkMessage(const uint8_t* payload, uint16_t length);
  void serviceUplinkQueue();
  void serviceDownlinkQueue();

  static constexpr uint8_t MAX_QUEUE_DEPTH = 32;

  struct QueueEntry {
    uint16_t length;
    uint8_t payload[link_protocol::FRAME_MAX_PAYLOAD];
  };

  Stream& m_linkIo;
  Rf23Driver& m_rf;
  LinkCounters& m_counters;
  RelayConfig m_config;

  ParseState m_state;
  uint8_t m_framePayload[link_protocol::FRAME_MAX_PAYLOAD];
  uint16_t m_frameLength;
  uint16_t m_frameIndex;
  uint16_t m_frameCrc;
  uint8_t m_crcLo;
  bool m_inCommandMode;
  char m_commandBuffer[link_protocol::COMMAND_MAX_LEN];
  size_t m_commandIndex;
  uint32_t m_lastFrameByteMs;

  // Boot-time Pi<->Teensy link handshake. Before the link is established the
  // Teensy answers the Pi's '#PING' locally with '#PONG' (never relayed over
  // RF) so the Pi can confirm the UART is alive before starting F'. Once a
  // PONG is sent the link latches established and byte inspection stops: the
  // bridge is a pure transparent tunnel from then on, so '#' bytes inside F'
  // binary traffic can never be misread as a command.
  bool m_inHandshake;
  bool m_linkEstablished;

  uint8_t m_nextMsgId;
  bool m_seenRxMsgId;
  uint8_t m_lastRxMsgId;
  bool m_reassemblyActive;
  uint8_t m_expectedMsgId;
  uint8_t m_expectedSegIndex;
  uint8_t m_expectedSegCount;
  uint16_t m_reassemblyLen;
  uint8_t m_reassemblyBuf[link_protocol::FRAME_MAX_PAYLOAD];
  uint32_t m_lastSegmentMs;

  uint8_t m_rawUartBuf[link_protocol::FRAME_MAX_PAYLOAD];
  uint16_t m_rawUartLen;
  uint32_t m_lastRawUartByteMs;

  QueueEntry m_uplinkQueue[MAX_QUEUE_DEPTH];
  uint8_t m_uplinkHead;
  uint8_t m_uplinkTail;
  uint8_t m_uplinkCount;

  QueueEntry m_downlinkQueue[MAX_QUEUE_DEPTH];
  uint8_t m_downlinkHead;
  uint8_t m_downlinkTail;
  uint8_t m_downlinkCount;
};

#endif
