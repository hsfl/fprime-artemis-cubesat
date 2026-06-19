#ifndef Components_CommsManager_HPP
#define Components_CommsManager_HPP

#include "Components/CommsManager/CommsManagerComponentAc.hpp"

namespace Components {

class CommsManager final : public CommsManagerComponentBase {
  public:
    CommsManager(const char* const compName);
    ~CommsManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void linkStatusIn_handler(FwIndexType portNum, U32 key) override;
    void scienceReadyIn_handler(FwIndexType portNum, U32 productBytes) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
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
    void SELECT_RADIO_BACKEND_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 backend) override;

    U32 m_linkState;
    U32 m_pendingScienceBytes;
    U32 m_radioBackend;
    U32 m_linkPollCount;
    U32 m_activeDownlinkBytes;
    U32 m_lastPayloadDownlinkState;
    bool m_downlinkActive;
};

}  // namespace Components

#endif
