#ifndef Components_CommsManager_HPP
#define Components_CommsManager_HPP

#include "Components/CommsManager/CommsManagerComponentAc.hpp"

#include <string>

namespace Components {

class CommsManager final : public CommsManagerComponentBase {
  public:
    CommsManager(const char* const compName);
    ~CommsManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void linkStatusIn_handler(FwIndexType portNum, U32 key) override;
    void scienceReadyIn_handler(FwIndexType portNum,
                                U32 productId,
                                U32 productBytes,
                                const Components::ScienceProductSource& sourceKind,
                                const Fw::StringBase& sourcePath,
                                U32 sourceCrc) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void rssiStatusIn_handler(FwIndexType portNum, I32 rssiDbm) override;
    void payloadDownlinkStatusIn_handler(
        FwIndexType portNum,
        U32 state,
        U32 transferId,
        U32 productId,
        U32 totalBytes,
        U32 packetsSent,
        U32 totalPackets,
        U32 lastError
    ) override;
    void REQUEST_SCIENCE_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REQUEST_LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void PING_LINK_RSSI_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void requestAdapterStatus();

    U32 m_linkState;
    I32 m_rssiDbm;
    bool m_rssiPingPending;
    U32 m_pendingProductId;
    U32 m_pendingScienceBytes;
    Components::ScienceProductSource m_pendingSourceKind;
    Fw::String m_pendingSourcePath;
    U32 m_pendingSourceCrc;
    U32 m_linkPollCount;
    U32 m_activeDownlinkBytes;
    U32 m_lastPayloadDownlinkState;
    bool m_downlinkActive;
};

}  // namespace Components

#endif
