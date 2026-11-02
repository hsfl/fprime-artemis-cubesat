#ifndef ARTEMIS_TEENSY_RELAY_UART_RF_HPP
#define ARTEMIS_TEENSY_RELAY_UART_RF_HPP

#include <Arduino.h>

#include "firmware_profile.hpp"
#include "link_counters.hpp"
#include "link_protocol.hpp"
#if !defined(ARTEMIS_CDR_NO_RADIO)
#include "rf23_driver.hpp"
#endif

class LocalChannelHandler {
 public:
  virtual bool beginLocalFrame(const uint8_t* payload, uint16_t length) = 0;
  virtual bool pollLocalResponse(uint8_t* payload, uint16_t& length) = 0;
};

struct RelayConfig {
  bool enablePiUart = true;
  bool uartOutputFramed = true;
  bool enableCommandMode = true;
  bool uartInputFramed = true;
  uint16_t rawUartFlushMs = 8;
  uint8_t uplinkQueueDepth = 16;
  uint8_t downlinkQueueDepth = 16;
  uint16_t rawUartChunkBytes = link_protocol::RF_SEGMENT_MAX_DATA;
  uint8_t defaultUartChannel = link_protocol::CHANNEL_CCSDS;
};

class RelayUartRf {
 public:
#if defined(ARTEMIS_CDR_NO_RADIO)
  RelayUartRf(Stream& linkIo,
              LinkCounters& counters,
              const RelayConfig& config = RelayConfig{},
              Stream* payloadIo = nullptr,
              LocalChannelHandler* localHandler = nullptr);
#else
  RelayUartRf(Stream& linkIo,
              Rf23Driver& rfDriver,
              LinkCounters& counters,
              const RelayConfig& config = RelayConfig{},
              Stream* payloadIo = nullptr,
              LocalChannelHandler* localHandler = nullptr);
#endif

  void begin();
  void poll();

 private:
  enum class ParseState {
    WAIT_MAGIC_0,
    WAIT_MAGIC_1,
    WAIT_CHANNEL,
    WAIT_LEN_LO,
    WAIT_LEN_HI,
    WAIT_PAYLOAD,
    WAIT_CRC_LO,
    WAIT_CRC_HI
  };

  void processUartByte(uint8_t b);
#if !defined(ARTEMIS_CDR_NO_RADIO)
  void processRawUartByte(uint8_t b, uint8_t channel);
  void flushRawUartIfStale();
  void flushPayloadUartIfStale();
#endif
  void flushLocalResponseToUart();
  void processCommandByte(uint8_t b);
  void processFrameByte(uint8_t b);
#if !defined(ARTEMIS_CDR_NO_RADIO)
  void flushRfToUart();
#endif

  void resetFrameParser(bool timeoutReset);
  void handleCompletedFrame();
  bool sendUartFrame(uint8_t channel, const uint8_t* payload, uint16_t length);
#if !defined(ARTEMIS_CDR_NO_RADIO)
  bool sendRawToUart(const uint8_t* payload, uint16_t length);

  bool sendPayloadOverRf(uint8_t channel, const uint8_t* payload, uint16_t length);
  bool sendRfPacketWithAck(const uint8_t* packet, uint8_t packetLen, uint8_t channel, uint8_t msgId, uint8_t segIdx);
  bool waitForAck(uint8_t channel, uint8_t msgId, uint8_t segIdx);
  bool isAckPacket(const uint8_t* packet, uint8_t packetLen, uint8_t channel, uint8_t msgId, uint8_t segIdx) const;
  bool sendAck(uint8_t channel, uint8_t msgId, uint8_t segIdx);
  void processRfSegment(const uint8_t* packet, uint8_t packetLen);
  void resetReassembly(uint8_t channel, bool timeoutReset, bool dropReset);
#endif

  uint16_t crc16Ccitt(const uint8_t* data, uint16_t len) const;
  void emitLinkStatus();
#if !defined(ARTEMIS_CDR_NO_RADIO)
  bool enqueueUplinkMessage(uint8_t channel, const uint8_t* payload, uint16_t length);
  bool enqueueDownlinkMessage(uint8_t channel, const uint8_t* payload, uint16_t length);
  void serviceUplinkQueue();
  void serviceDownlinkQueue();
#endif

  static constexpr uint8_t MAX_QUEUE_DEPTH = 32;

  struct QueueEntry {
    uint8_t channel;
    uint16_t length;
    uint8_t payload[link_protocol::FRAME_MAX_PAYLOAD];
  };

#if !defined(ARTEMIS_CDR_NO_RADIO)
  struct ReassemblyState {
    bool seenRxMsgId;
    uint8_t lastRxMsgId;
    bool active;
    uint8_t expectedMsgId;
    uint8_t expectedSegIndex;
    uint8_t expectedSegCount;
    uint16_t length;
    uint8_t buffer[link_protocol::FRAME_MAX_PAYLOAD];
    uint32_t lastSegmentMs;
  };
#endif

  Stream& m_linkIo;
#if !defined(ARTEMIS_CDR_NO_RADIO)
  Stream* m_payloadIo;
#endif
  LocalChannelHandler* m_localHandler;
#if !defined(ARTEMIS_CDR_NO_RADIO)
  Rf23Driver& m_rf;
#endif
  LinkCounters& m_counters;
  RelayConfig m_config;

  ParseState m_state;
  uint8_t m_frameChannel;
  uint8_t m_framePayload[link_protocol::FRAME_MAX_PAYLOAD];
  uint16_t m_frameLength;
  uint16_t m_frameIndex;
  uint16_t m_frameCrc;
  uint8_t m_crcLo;
  bool m_inCommandMode;
  char m_commandBuffer[link_protocol::COMMAND_MAX_LEN];
  size_t m_commandIndex;
  uint32_t m_lastFrameByteMs;

#if !defined(ARTEMIS_CDR_NO_RADIO)
  uint8_t m_nextMsgId;
  ReassemblyState m_reassembly[link_protocol::CHANNEL_COUNT];

  uint8_t m_rawUartBuf[link_protocol::FRAME_MAX_PAYLOAD];
  uint16_t m_rawUartLen;
  uint32_t m_lastRawUartByteMs;
  uint8_t m_payloadUartBuf[link_protocol::FRAME_MAX_PAYLOAD];
  uint16_t m_payloadUartLen;
  uint32_t m_lastPayloadUartByteMs;

  QueueEntry m_uplinkQueue[MAX_QUEUE_DEPTH];
  uint8_t m_uplinkHead;
  uint8_t m_uplinkTail;
  uint8_t m_uplinkCount;

  QueueEntry m_downlinkQueue[MAX_QUEUE_DEPTH];
  uint8_t m_downlinkHead;
  uint8_t m_downlinkTail;
  uint8_t m_downlinkCount;
#endif
};

#endif
