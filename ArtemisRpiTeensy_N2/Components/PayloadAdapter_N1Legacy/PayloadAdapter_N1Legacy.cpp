#include "Components/PayloadAdapter_N1Legacy/PayloadAdapter_N1Legacy.hpp"

namespace Components {

PayloadAdapter_N1Legacy::PayloadAdapter_N1Legacy(const char* const compName)
    : PayloadAdapter_N1LegacyComponentBase(compName),
      m_lastRequestKey(0) {}

PayloadAdapter_N1Legacy::~PayloadAdapter_N1Legacy() {}

void PayloadAdapter_N1Legacy::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadAdapter_N1Legacy::requestIn_handler(FwIndexType portNum, U32 key) {
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
