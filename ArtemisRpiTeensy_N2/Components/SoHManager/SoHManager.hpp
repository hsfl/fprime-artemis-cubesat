#ifndef Components_SoHManager_HPP
#define Components_SoHManager_HPP

#include "Components/SoHManager/SoHManagerComponentAc.hpp"

namespace Components {

class SoHManager final : public SoHManagerComponentBase {
  public:
    SoHManager(const char* const compName);
    ~SoHManager();

  private:
    enum SoHStatusSlots {
        SLOT_EPS = 0,
        SLOT_PAYLOAD = 1,
        SLOT_ADCS = 2,
        SLOT_GPS = 3,
        SLOT_STORAGE = 4,
        SLOT_THERMAL = 5,
        SLOT_COMMS = 6,
        SLOT_TRANSPORT = 7,
        SLOT_COUNT = 8,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void statusIn_handler(FwIndexType portNum, U32 key) override;
    void EMIT_SOH_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    U32 m_status[SLOT_COUNT];
};

}  // namespace Components

#endif
