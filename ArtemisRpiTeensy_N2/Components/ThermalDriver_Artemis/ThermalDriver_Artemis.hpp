#ifndef Components_ThermalDriver_Artemis_HPP
#define Components_ThermalDriver_Artemis_HPP

#include "Components/ThermalDriver_Artemis/ThermalDriver_ArtemisComponentAc.hpp"

namespace Components {

class ThermalDriver_Artemis final : public ThermalDriver_ArtemisComponentBase {
  public:
    ThermalDriver_Artemis(const char* const compName);
    ~ThermalDriver_Artemis();

  private:
    enum class ThermalState : U32 {
        NOMINAL = 0,
        WARMING = 1,
        HOT = 2,
        COLD = 3,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;

    void updateThermalModel();
    U32 toStatusKey() const;

    U32 m_lastRequestKey;
    U32 m_requestCount;
    ThermalState m_thermalState;
    I32 m_obcTempCentiC;
    I32 m_batteryTempCentiC;
};

}  // namespace Components

#endif
