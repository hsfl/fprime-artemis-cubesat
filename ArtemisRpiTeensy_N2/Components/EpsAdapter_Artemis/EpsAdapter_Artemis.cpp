#include "Components/EpsAdapter_Artemis/EpsAdapter_Artemis.hpp"

namespace Components {

EpsAdapter_Artemis::EpsAdapter_Artemis(const char* const compName)
    : EpsAdapter_ArtemisComponentBase(compName),
      m_lastRequestKey(0) {}

EpsAdapter_Artemis::~EpsAdapter_Artemis() {}

void EpsAdapter_Artemis::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void EpsAdapter_Artemis::requestIn_handler(FwIndexType portNum, U32 key) {
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
