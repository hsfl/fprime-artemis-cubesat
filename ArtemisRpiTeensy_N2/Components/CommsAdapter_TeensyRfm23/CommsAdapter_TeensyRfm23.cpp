#include "Components/CommsAdapter_TeensyRfm23/CommsAdapter_TeensyRfm23.hpp"

namespace Components {

CommsAdapter_TeensyRfm23::CommsAdapter_TeensyRfm23(const char* const compName)
    : CommsAdapter_TeensyRfm23ComponentBase(compName),
      m_lastRequestKey(0) {}

CommsAdapter_TeensyRfm23::~CommsAdapter_TeensyRfm23() {}

void CommsAdapter_TeensyRfm23::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void CommsAdapter_TeensyRfm23::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_lastRequestKey = key;
    const U32 status = key + STATUS_OFFSET;
    this->tlmWrite_LastRequestKey(this->m_lastRequestKey);
    this->log_ACTIVITY_LO_RequestHandled(this->m_lastRequestKey, status);
    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0, status);
    }
    if (this->isConnected_statusOut_OutputPort(1)) {
        this->statusOut_out(1, status);
    }
}

}  // namespace Components
