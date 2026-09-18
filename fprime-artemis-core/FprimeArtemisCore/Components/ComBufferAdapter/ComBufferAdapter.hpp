// ======================================================================
// \title  ComBufferAdapter.hpp
// \brief  hpp file for ComBufferAdapter component implementation class
// ======================================================================

#ifndef Components_ComBufferAdapter_HPP
#define Components_ComBufferAdapter_HPP

#include "FprimeArtemisCore/Components/ComBufferAdapter/ComBufferAdapterComponentAc.hpp"

namespace Components {

class ComBufferAdapter final : public ComBufferAdapterComponentBase {
  public:
    //! Construct ComBufferAdapter object
    explicit ComBufferAdapter(const char* const compName);

    //! Destroy ComBufferAdapter object
    ~ComBufferAdapter();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Outgoing hub message: hand it to the framer
    void bufferIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    //! Framer is done with an outgoing message: give ownership back to the hub
    void comDataReturnIn_handler(FwIndexType portNum,
                                 Fw::Buffer& data,
                                 const ComCfg::FrameContext& context) override;

    //! Incoming deframed message: hand it to the hub
    void comDataIn_handler(FwIndexType portNum,
                           Fw::Buffer& data,
                           const ComCfg::FrameContext& context) override;

    //! Hub is done with an incoming message: give ownership back to the deframer
    void bufferOutReturn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    //! Context for messages handed to the framer. Hub messages are not F Prime
    //! packets, so there is no meaningful APID to set; the default is used.
    ComCfg::FrameContext m_emptyContext;
};

}  // namespace Components

#endif
