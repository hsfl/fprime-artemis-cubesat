#ifndef Components_CommsDriver_TeensyRfm23_HPP
#define Components_CommsDriver_TeensyRfm23_HPP

#include "Components/CommsDriver_TeensyRfm23/CommsDriver_TeensyRfm23ComponentAc.hpp"
#include "Components/LinkCfg/LinkCfg.hpp"

namespace Components {

class CommsDriver_TeensyRfm23 final : public CommsDriver_TeensyRfm23ComponentBase {
  public:
    CommsDriver_TeensyRfm23(const char* const compName);
    ~CommsDriver_TeensyRfm23();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum,
                           const Components::RadioOperation& operation,
                           U8 enabled) override;
    void teensyResponseIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    bool sendRequest(const Components::RadioOperation& operation, U8 enabled);
    bool parseStatusResponse(const U8* data, FwSizeType size);
    bool parseSetEnabledResponse(const U8* data, FwSizeType size);
    void emitStatus(const Components::RadioOperation& operation,
                    const Components::RadioRpcResult& result);
    void rejectResponse(U32 reason, U8 receivedId, bool clearPending);
    void clearPending();
    void timeoutPendingRequest();
    static bool validState(U8 state);
    static bool validFault(U8 fault);
    static Components::RadioRpcResult mapLocalStatus(U8 status);
    static U16 readLe16(const U8* data);
    static U32 readLe32(const U8* data);

    static constexpr U8 LOCAL_HEADER_LEN = 4U;
    static constexpr U8 STATUS_RESPONSE_PAYLOAD_LEN = 33U;
    static constexpr U8 SET_ENABLED_RESPONSE_PAYLOAD_LEN = 4U;
    static constexpr U32 REQUEST_TIMEOUT_TICKS = 15U;

    U8 m_sequence;
    U8 m_pendingRequestId;
    Components::RadioOperation m_pendingOperation;
    U8 m_pendingEnabled;
    U32 m_pendingRequestTicks;
    bool m_requestPending;
    U32 m_requestCount;
    U32 m_rpcFailureCount;
    U32 m_rejectedResponseCount;
    Components::RadioState m_radioState;
    Components::RadioFault m_radioFault;
    U8 m_bootFlags;
    U8 m_rssiValid;
    I32 m_rssiDbm;
    U32 m_rssiAgeMs;
    U32 m_initAttempts;
    U32 m_rfRxPackets;
    U32 m_rfTxPackets;
    U32 m_rfTxDrops;
    U8 m_localPacket[6];
};

}  // namespace Components

#endif
