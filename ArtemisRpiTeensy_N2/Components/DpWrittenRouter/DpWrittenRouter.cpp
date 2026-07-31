#include "Components/DpWrittenRouter/DpWrittenRouter.hpp"

namespace Components {

DpWrittenRouter::DpWrittenRouter(const char* const compName) : DpWrittenRouterComponentBase(compName) {}

DpWrittenRouter::~DpWrittenRouter() {}

void DpWrittenRouter::dpWrittenIn_handler(FwIndexType portNum,
                                          const Fw::StringBase& fileName,
                                          FwDpPriorityType priority,
                                          FwSizeType size) {
    static_cast<void>(portNum);
    if (this->isConnected_catalogOut_OutputPort(0)) {
        this->catalogOut_out(0, fileName, priority, size);
    }
    if (this->isConnected_leptonNotifyOut_OutputPort(0)) {
        this->leptonNotifyOut_out(0, fileName, priority, size);
    }
    if (this->isConnected_bosonNotifyOut_OutputPort(0)) {
        this->bosonNotifyOut_out(0, fileName, priority, size);
    }
}

}  // namespace Components
