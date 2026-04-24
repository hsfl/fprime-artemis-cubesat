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
    void scienceReadyIn_handler(FwIndexType portNum, U32 key) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_SCIENCE_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    U32 m_linkState;
    U32 m_pendingScienceBytes;
};

}  // namespace Components

#endif
