#include "Components/PingResponder/PingResponder.hpp"

namespace Components {

PingResponder::PingResponder(const char* const compName)
    : PingResponderComponentBase(compName) {}

PingResponder::~PingResponder() {}

void PingResponder::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

}  // namespace Components
