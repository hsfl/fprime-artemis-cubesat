#ifndef Components_ScienceApp_HPP
#define Components_ScienceApp_HPP

#include "Components/ScienceApp/ScienceAppComponentAc.hpp"

namespace Components {

class ScienceApp final : public ScienceAppComponentBase {
  public:
    ScienceApp(const char* const compName);
    ~ScienceApp();
    void preamble() override;

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum, U32 delaySeconds) override;
    void cancelRequestIn_handler(FwIndexType portNum, U32 key) override;
    void payloadStatusIn_handler(FwIndexType portNum,
                                 U32 productId,
                                 U32 productBytes,
                                 const Components::ScienceProductSource& sourceKind,
                                 const Fw::StringBase& sourcePath,
                                 U32 sourceCrc) override;
    void START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void CONFIGURE_CAPTURE_DURATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
    void SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
    void parameterUpdated(FwPrmIdType id) override;
    void seedCaptureDurationFromParam();
    bool isValidCaptureDuration(U32 durationSeconds) const;
    void cancelPendingCollection();
    void writeTelemetry();

    U32 m_pendingDelaySeconds;
    U32 m_captureDurationSeconds;
    U32 m_collectionCount;
    U32 m_runTicks;
    U32 m_lastTelemetryTick;
};

}  // namespace Components

#endif
