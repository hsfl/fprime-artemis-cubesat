#ifndef Components_PayloadDownlinkManager_HPP
#define Components_PayloadDownlinkManager_HPP

#include "Components/LinkCfg/LinkCfg.hpp"
#include "Components/PayloadDownlinkManager/PayloadDownlinkManagerComponentAc.hpp"

namespace Components {

class PayloadDownlinkManager final : public PayloadDownlinkManagerComponentBase {
  public:
    PayloadDownlinkManager(const char* const compName);
    ~PayloadDownlinkManager();

  private:
    enum PacketType : U8 {
        PACKET_HEADER = 1,
        PACKET_DATA = 2,
        PACKET_END = 3,
        PACKET_RETRY_REQUEST = 4
    };

    enum State : U32 {
        STATE_IDLE = 0,
        STATE_DOWNLINKING = 1,
        STATE_DONE = 2,
        STATE_ABORTED = 3,
        STATE_ERROR = 4
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void packetIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void downlinkRequestIn_handler(FwIndexType portNum, U32 key) override;
    void START_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 productId, U32 byteCount) override;
    void ABORT_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void GET_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    void resetTransfer(U32 productId, U32 byteCount);
    void emitTelemetry();
    void sendHeaderPacket();
    void sendDataPacket(U32 packetIndex);
    void sendEndPacket();
    void sendPacket(const U8* data, FwSizeType size);
    void handleRetryRequest(const U8* data, FwSizeType size);
    U8 blobByteAt(U32 offset) const;
    U16 blobCrc(U32 byteCount) const;
    U16 crc16Ccitt(const U8* data, FwSizeType size) const;
    void putU16(U8* data, FwSizeType offset, U16 value) const;
    void putU32(U8* data, FwSizeType offset, U32 value) const;
    U16 getU16(const U8* data, FwSizeType offset) const;

    State m_state;
    U8 m_transferId;
    U32 m_productId;
    U32 m_totalBytes;
    U32 m_totalPackets;
    U32 m_nextPacketIndex;
    U32 m_packetsSent;
    U32 m_retryRound;
    U32 m_packetsMissing;
    U32 m_lastError;
    U16 m_blobCrc;
    bool m_sentHeader;
    bool m_sentEnd;
    U8 m_packet[LinkCfg::PAYLOAD_PACKET_MAX_BYTES];
};

}  // namespace Components

#endif
