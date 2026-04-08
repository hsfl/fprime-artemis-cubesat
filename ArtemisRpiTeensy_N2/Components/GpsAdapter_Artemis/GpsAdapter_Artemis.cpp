#include "Components/GpsAdapter_Artemis/GpsAdapter_Artemis.hpp"

namespace Components {

GpsAdapter_Artemis::GpsAdapter_Artemis(const char* const compName)
    : GpsAdapter_ArtemisComponentBase(compName),
      m_lastRequestKey(0) {}

GpsAdapter_Artemis::~GpsAdapter_Artemis() {}

void GpsAdapter_Artemis::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void GpsAdapter_Artemis::requestIn_handler(FwIndexType portNum, U32 key) {
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
