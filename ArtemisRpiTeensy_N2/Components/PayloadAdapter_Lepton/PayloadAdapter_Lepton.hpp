// ======================================================================
// \title  PayloadAdapter_Lepton.hpp
// \author samanthamallari
// \brief  hpp file for PayloadAdapter_Lepton component implementation class
// ======================================================================

#ifndef Components_PayloadAdapter_Lepton_HPP
#define Components_PayloadAdapter_Lepton_HPP

#include "Components/PayloadAdapter_Lepton/PayloadAdapter_LeptonComponentAc.hpp"

namespace Components {

class PayloadAdapter_Lepton final : public PayloadAdapter_LeptonComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct PayloadAdapter_Lepton object
    PayloadAdapter_Lepton(const char* const compName  //!< The component name
    );

    //! Destroy PayloadAdapter_Lepton object
    ~PayloadAdapter_Lepton();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command CAPTURE_IMAGE
    //!
    //! Capture a thermal image from the Lepton camera and store it as a data product.
    //! Currently a stub: fills the image with a test ramp instead of reading the camera.
    void CAPTURE_IMAGE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                  U32 cmdSeq            //!< The command sequence number
                                  ) override;
};

}  // namespace Components

#endif
