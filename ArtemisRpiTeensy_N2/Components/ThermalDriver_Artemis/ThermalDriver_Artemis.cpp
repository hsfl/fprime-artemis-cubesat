#include "Components/ThermalDriver_Artemis/ThermalDriver_Artemis.hpp"

namespace Components {

ThermalDriver_Artemis::ThermalDriver_Artemis(const char* const compName)
    : ThermalDriver_ArtemisComponentBase(compName),
      m_lastRequestKey(0),
      m_requestCount(0),
      m_thermalState(ThermalState::NOMINAL),
      m_obcTempCentiC(2400),
      m_batteryTempCentiC(2200) {}

ThermalDriver_Artemis::~ThermalDriver_Artemis() {}

void ThermalDriver_Artemis::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void ThermalDriver_Artemis::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_lastRequestKey = key;
    this->m_requestCount += 1;

    const ThermalState previousState = this->m_thermalState;
    this->updateThermalModel();
    const U32 status = this->toStatusKey();

    this->tlmWrite_LastRequestKey(this->m_lastRequestKey);
    this->tlmWrite_RequestCount(this->m_requestCount);
    this->tlmWrite_ThermalState(status);
    this->tlmWrite_ObcTempCentiC(this->m_obcTempCentiC);
    this->tlmWrite_BatteryTempCentiC(this->m_batteryTempCentiC);

    if (previousState != this->m_thermalState) {
        this->log_ACTIVITY_HI_ThermalStateChanged(static_cast<U32>(previousState), status);
    }

    this->log_ACTIVITY_LO_RequestHandled(this->m_lastRequestKey, status);

    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0, status);
    }
}

void ThermalDriver_Artemis::updateThermalModel() {
    const I32 obcDelta = static_cast<I32>((this->m_requestCount % 9U) * 25U);
    const I32 batteryDelta = static_cast<I32>((this->m_requestCount % 7U) * 20U);

    this->m_obcTempCentiC = 2200 + obcDelta;
    this->m_batteryTempCentiC = 2000 + batteryDelta;

    if ((this->m_requestCount % 53U) == 0U) {
        this->m_thermalState = ThermalState::HOT;
        this->m_obcTempCentiC = 4100;
        return;
    }

    if ((this->m_requestCount % 37U) == 0U) {
        this->m_thermalState = ThermalState::COLD;
        this->m_batteryTempCentiC = 200;
        return;
    }

    if (this->m_requestCount < 3U) {
        this->m_thermalState = ThermalState::WARMING;
        return;
    }

    this->m_thermalState = ThermalState::NOMINAL;
}

U32 ThermalDriver_Artemis::toStatusKey() const {
    return static_cast<U32>(this->m_thermalState);
}

}  // namespace Components
