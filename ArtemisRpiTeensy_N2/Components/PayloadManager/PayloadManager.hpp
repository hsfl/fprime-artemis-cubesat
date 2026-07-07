#ifndef Components_PayloadManager_HPP
#define Components_PayloadManager_HPP

#include "Components/PayloadManager/PayloadManagerComponentAc.hpp"

namespace Components {

class PayloadManager final : public PayloadManagerComponentBase {
  public:
    PayloadManager(const char* const compName);
    ~PayloadManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum, U32 durationSeconds) override;
    void driverStatusIn_handler(FwIndexType portNum,
                                 U32 productId,
                                 U32 productBytes,
                                 const Components::ScienceProductSource& sourceKind,
                                 const Fw::StringBase& sourcePath,
                                 U32 sourceCrc) override;
    void REQUEST_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void CONFIGURE_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 sampleCount, U32 periodMs) override;
    void START_PAYLOAD_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 collectionId) override;
    void SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;

    U32 m_lastPayloadValue;
    U32 m_lastCollectionId;
    U32 m_lastCaptureDurationSeconds;
    U32 m_sampleCount;
    U32 m_samplePeriodMs;
    U32 m_managerHeartbeat;
};

}  // namespace Components

#endif
