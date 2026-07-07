// ======================================================================
// \title  PayloadAdapter_Lepton.hpp
// \author samanthamallari
// \brief  hpp file for PayloadAdapter_Lepton component implementation class
// ======================================================================

#ifndef Components_PayloadAdapter_Lepton_HPP
#define Components_PayloadAdapter_Lepton_HPP

#include "Components/PayloadAdapter_Lepton/LeptonCamera.hpp"
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

    //! Handler implementation for command ENABLE
    //!
    //! Bring the Lepton camera up and start its continuous thermal stream.
    void ENABLE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                           U32 cmdSeq            //!< The command sequence number
                           ) override;

    //! Handler implementation for command DISABLE
    //!
    //! Stop the Lepton stream and release the camera.
    void DISABLE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                            U32 cmdSeq            //!< The command sequence number
                            ) override;

    //! Handler implementation for command CAPTURE_IMAGE
    //!
    //! Copy the latest streamed frame from the Lepton camera and store it as a
    //! data product. Requires the camera to have been ENABLEd first.
    void CAPTURE_IMAGE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                  U32 cmdSeq            //!< The command sequence number
                                  ) override;

    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler for the scheduled-collection capture request from the science chain.
    //! Captures one frame (durationSeconds ignored) and emits a ScienceProductDescriptor
    //! on statusOut so StorageService/CommsManager can stage the resulting .fdp.
    void requestIn_handler(FwIndexType portNum,     //!< The port number
                           U32 durationSeconds      //!< Requested capture duration (ignored)
                           ) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Capture the latest streamed frame and store it as a data product.
    //! Shared by CAPTURE_IMAGE and requestIn. Logs success/failure events.
    //! @return true if the frame was captured and handed to the DP writer.
    bool captureThermalImage();

    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! Thermal camera driver (real libuvc on the Pi, ramp stub on dev hosts)
    LeptonCamera camera;

    //! Monotonic counter used as the science product id reported on statusOut
    U32 m_captureCount;

    //! Max time to wait for a valid frame after the stream is up (covers FFC)
    static constexpr U32 CAPTURE_TIMEOUT_MS = 5000;

    //! On-disk size of one thermal-image data product (Dp_*.fdp): 160*120 U16
    //! image bytes plus DpWriter container framing. Must equal the actual file
    //! size so PayloadDownlinkManager's size check accepts the latest .fdp.
    //! Update if the image resolution/type or DP framing changes.
    static constexpr U32 THERMAL_PRODUCT_BYTES = 38480;
};

}  // namespace Components

#endif
