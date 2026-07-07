#include "Components/AdcsDriver_D2S2/AdcsDriver_D2S2.hpp"

namespace Components {

AdcsDriver_D2S2::AdcsDriver_D2S2(const char* const compName)
    : AdcsDriver_D2S2ComponentBase(compName),
      m_lastRequestKey(0) {}

AdcsDriver_D2S2::~AdcsDriver_D2S2() {}

void AdcsDriver_D2S2::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void AdcsDriver_D2S2::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_lastRequestKey = key;
    const U32 status = key + STATUS_OFFSET;
    this->tlmWrite_LastRequestKey(this->m_lastRequestKey);
    this->log_ACTIVITY_LO_RequestHandled(this->m_lastRequestKey, status);
    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0, status);
    }
}

}  // namespace Components
