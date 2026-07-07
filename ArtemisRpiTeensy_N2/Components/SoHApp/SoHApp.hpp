#ifndef Components_SoHApp_HPP
#define Components_SoHApp_HPP

#include "Components/SoHApp/SoHAppComponentAc.hpp"

namespace Components {

class SoHApp final : public SoHAppComponentBase {
  public:
    SoHApp(const char* const compName);
    ~SoHApp();

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
    void statusIn_handler(FwIndexType portNum, const Components::HealthState& health, U32 detail) override;
    void EMIT_SOH_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    Components::HealthState overallHealth() const;

    Components::HealthState m_status[SLOT_COUNT];
    U32 m_detail[SLOT_COUNT];
};

}  // namespace Components

#endif
