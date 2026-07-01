#ifndef Components_ThermalAdapter_Artemis_HPP
#define Components_ThermalAdapter_Artemis_HPP

#include "Components/ThermalAdapter_Artemis/ThermalAdapter_ArtemisComponentAc.hpp"

namespace Components {

class ThermalAdapter_Artemis final : public ThermalAdapter_ArtemisComponentBase {
  public:
    ThermalAdapter_Artemis(const char* const compName);
    ~ThermalAdapter_Artemis();

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
