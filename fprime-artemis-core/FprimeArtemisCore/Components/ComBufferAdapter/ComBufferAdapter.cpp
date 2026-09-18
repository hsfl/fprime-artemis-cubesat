// ======================================================================
// \title  ComBufferAdapter.cpp
// \brief  cpp file for ComBufferAdapter component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/ComBufferAdapter/ComBufferAdapter.hpp"

namespace Components {

ComBufferAdapter::ComBufferAdapter(const char* const compName)
    : ComBufferAdapterComponentBase(compName), m_emptyContext() {}

ComBufferAdapter::~ComBufferAdapter() {}

// ----------------------------------------------------------------------
// Downlink: hub -> framer
// ----------------------------------------------------------------------

void ComBufferAdapter::bufferIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    // The framer allocates its own frame buffer and returns this one on
    // comDataReturnIn, so ownership is not released here.
    this->comDataOut_out(0, fwBuffer, this->m_emptyContext);
}

void ComBufferAdapter::comDataReturnIn_handler(FwIndexType portNum,
                                             Fw::Buffer& data,
                                             const ComCfg::FrameContext& context) {
    // Framing is done with the hub's buffer: hand ownership back to the hub,
    // which deallocates it.
    this->bufferInReturn_out(0, data);
}

// ----------------------------------------------------------------------
// Uplink: deframer -> hub
// ----------------------------------------------------------------------

void ComBufferAdapter::comDataIn_handler(FwIndexType portNum,
                                       Fw::Buffer& data,
                                       const ComCfg::FrameContext& context) {
    // The hub returns this buffer on bufferOutReturn once it (and any buffer
    // consumer downstream of it) is finished with the payload.
    this->bufferOut_out(0, data);
}

void ComBufferAdapter::bufferOutReturn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    // Hub is done with the deframed message: return it up the receive chain so
    // the FrameAccumulator's allocator gets its buffer back.
    this->comDataReturnOut_out(0, fwBuffer, this->m_emptyContext);
}

}  // namespace Components
