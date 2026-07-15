#ifndef Components_PayloadDownlinkApp_HPP
#define Components_PayloadDownlinkApp_HPP

#include "Components/LinkCfg/LinkCfg.hpp"
#include "Components/PayloadDownlinkApp/PayloadDownlinkAppComponentAc.hpp"
#include "Os/Mutex.hpp"

#include <string>

namespace Components {

class PayloadDownlinkApp final : public PayloadDownlinkAppComponentBase {
  public:
    PayloadDownlinkApp(const char* const compName);
    ~PayloadDownlinkApp();

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

    enum RequestDisposition : U32 {
        REQUEST_ACCEPTED = 0,
        REQUEST_DUPLICATE = 1,
        REQUEST_CONFLICT = 2
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void packetIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void downlinkRequestIn_handler(FwIndexType portNum,
                                   U32 productId,
                                   U32 productBytes,
                                   const Components::ScienceProductSource& sourceKind,
                                   const Fw::StringBase& sourcePath,
                                   U32 sourceCrc) override;
    void START_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 productId, U32 byteCount) override;
    void ABORT_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void GET_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    bool activeRequestMatches(U32 productId,
                              U32 byteCount,
                              const Components::ScienceProductSource& sourceKind,
                              const std::string& requestedSourcePath,
                              U32 expectedSourceCrc) const;
    void reportDuplicateRequest();
    void reportConflictingRequest(U32 requestedProductId);
    void drainControlMailbox();
    void clearRepairWork();
    void rejectControlPacket(U32 reason);
    bool resetTransfer(U32 productId,
                       U32 byteCount,
                       const Components::ScienceProductSource& sourceKind,
                       const std::string& preferredSourcePath,
                       U32 expectedSourceCrc);
    bool prepareSource(U32 byteCount, const std::string& preferredSourcePath);
    void emitTelemetry(bool force = false);
    void emitStatus();
    void writeProgressTelemetry();
    Components::PayloadSendStatus sendHeaderPacket();
    Components::PayloadSendStatus sendDataPacket(U32 packetIndex);
    Components::PayloadSendStatus sendEndPacket();
    Components::PayloadSendStatus sendPacket(const U8* data, FwSizeType size);
    void updateProgressIfDue();
    void handleRetryRequest(const U8* data, FwSizeType size);
    bool readSourceBytes(U32 offset, U8* output, U32 length) const;
    bool computeSourceCrc(U32 byteCount, U16& crcOut) const;
    U16 crc16Ccitt(const U8* data, FwSizeType size) const;
    void putU16(U8* data, FwSizeType offset, U16 value) const;
    void putU32(U8* data, FwSizeType offset, U32 value) const;
    U16 getU16(const U8* data, FwSizeType offset) const;
    void failTransfer(U32 reason, U32 detail);

    static constexpr U32 MAX_RETRY_PACKETS = 8U * 36U;
    static constexpr U32 CONTROL_MAILBOX_CAPACITY = 8U;

    struct ControlPacket {
        FwSizeType size;
        U8 data[LinkCfg::PAYLOAD_PACKET_MAX_BYTES];
    };

    State m_state;
    U8 m_transferId;
    U32 m_productId;
    U32 m_totalBytes;
    U32 m_totalPackets;
    U32 m_nextPacketIndex;
    U32 m_packetsSent;
    U32 m_progressPercent;
    U32 m_retryRound;
    U32 m_packetsMissing;
    U32 m_lastError;
    RequestDisposition m_requestDisposition;
    U32 m_nextProgressPercent;
    U16 m_blobCrc;
    bool m_sentHeader;
    bool m_sentEnd;
    bool m_sourceReady;
    U32 m_sourceBytes;
    std::string m_sourcePath;
    Components::ScienceProductSource m_requestedSourceKind;
    std::string m_requestedSourcePath;
    U32 m_expectedSourceCrc;
    U32 m_runTicks;
    U32 m_lastTelemetryTick;
    U32 m_retryPackets[MAX_RETRY_PACKETS];
    U32 m_retryCount;
    U32 m_retryCursor;
    Os::Mutex m_controlMailboxMutex;
    ControlPacket m_controlMailbox[CONTROL_MAILBOX_CAPACITY];
    U32 m_controlMailboxHead;
    U32 m_controlMailboxTail;
    U32 m_controlMailboxCount;
    U32 m_controlMailboxDrops;
    U32 m_controlPacketsInvalid;
    U32 m_controlMailboxHighWater;
    U32 m_reportedControlMailboxDrops;
    U32 m_reportedControlPacketsInvalid;
    U8 m_packet[LinkCfg::PAYLOAD_PACKET_MAX_BYTES];
};

}  // namespace Components

#endif
