// ======================================================================
// \title  PayloadAdapter_Lepton.cpp
// \author samanthamallari
// \brief  cpp file for PayloadAdapter_Lepton component implementation class
// ======================================================================

#include "Components/PayloadAdapter_Lepton/PayloadAdapter_Lepton.hpp"

namespace Components {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

PayloadAdapter_Lepton ::PayloadAdapter_Lepton(const char* const compName)
    : PayloadAdapter_LeptonComponentBase(compName) {}

PayloadAdapter_Lepton ::~PayloadAdapter_Lepton() {}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

// Implementation of CAPTURE_IMAGE command handler: captures a thermal image and stores it as a data product.
void PayloadAdapter_Lepton ::CAPTURE_IMAGE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // The data-product get port must be connected or there is nowhere to store the image.
    if (!this->isConnected_productGetOut_OutputPort(0)) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    // @TODO: implement capture with libuvc, add error handling, and check for camera readiness before capturing an image.
    // for now: fills the image with dummy values
    ThermalImageRecordType record;
    const Fw::Time now = this->getTime();
    record.set_timeTag(Fw::TimeValue(now.getTimeBase(), now.getContext(), now.getSeconds(), now.getUSeconds()));
    // Fill the record's pixel array in place (avoids a second 38 KB stack buffer).
    U16(&pixels)[160 * 120] = record.get_value();
    for (U32 i = 0; i < (160 * 120); i++) {
        pixels[i] = static_cast<U16>(i);
    }

    // Allocate a data-product container large enough for one image record.
    const FwSizeType dpSize =
        ThermalImageRecordType::SERIALIZED_SIZE + static_cast<FwSizeType>(sizeof(FwDpIdType));
    DpContainer container;
    const Fw::Success status = this->dpGet_ThermalImageContainer(dpSize, container);
    if (Fw::Success::FAILURE == status) {
        this->log_WARNING_HI_DpMemoryFailure(dpSize);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    // Serialize the image into the container and hand it to the DP writer (stores to disk).
    container.serializeRecord_ThermalImageRecord(record);
    this->dpSend(container);

    this->log_ACTIVITY_HI_ImageCaptureSuccess(dpSize);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
