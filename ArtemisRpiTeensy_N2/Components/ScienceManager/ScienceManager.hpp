#ifndef Components_ScienceManager_HPP
#define Components_ScienceManager_HPP

#include "Components/ScienceManager/ScienceManagerComponentAc.hpp"

namespace Components {

class ScienceManager final : public ScienceManagerComponentBase {
  public:
    ScienceManager(const char* const compName);
    ~ScienceManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum, U32 delaySeconds) override;
    void payloadStatusIn_handler(FwIndexType portNum, U32 productBytes) override;
    void START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void CONFIGURE_CAPTURE_DURATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
    void SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;

    U32 m_pendingDelaySeconds;
    U32 m_captureDurationSeconds;
    U32 m_collectionCount;
};

}  // namespace Components

#endif
