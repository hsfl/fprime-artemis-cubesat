#ifndef Components_PayloadService_HPP
#define Components_PayloadService_HPP

#include "Components/PayloadService/PayloadServiceComponentAc.hpp"

namespace Components {

class PayloadService final : public PayloadServiceComponentBase {
  public:
    PayloadService(const char* const compName);
    ~PayloadService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void CONFIGURE_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 sampleCount, U32 periodMs) override;
    void START_PAYLOAD_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 collectionId) override;
    void SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
    void SET_PAYLOAD_SIM_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 enable) override;

    U32 m_lastPayloadValue;
    U32 m_lastCollectionId;
    U32 m_lastCaptureDurationSeconds;
    U32 m_sampleCount;
    U32 m_samplePeriodMs;
    U32 m_simModeEnabled;
    U32 m_serviceHeartbeat;
};

}  // namespace Components

#endif
