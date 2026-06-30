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

// Implementation of ENABLE command handler: brings the camera up and starts streaming.
void PayloadAdapter_Lepton ::ENABLE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    char reason[80] = {0};
    const LeptonCamera::Status status = camera.open(reason, sizeof(reason)); // returns DEVICE_ERROR if the camera is not connected or fails to stream
    if (LeptonCamera::OK != status) {
        Fw::LogStringArg reasonArg(reason);
        this->log_WARNING_HI_ImageCaptureFailed(reasonArg);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->log_ACTIVITY_LO_LeptonReady();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// Implementation of DISABLE command handler: stops streaming and releases the camera.
void PayloadAdapter_Lepton ::DISABLE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    camera.close();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// Implementation of CAPTURE_IMAGE command handler: grabs the latest streamed frame and stores it as a data product.
void PayloadAdapter_Lepton ::CAPTURE_IMAGE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // The data-product get port must be connected or there is nowhere to store the image.
    if (!this->isConnected_productGetOut_OutputPort(0)) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    this->log_ACTIVITY_LO_ImageCaptureStart();

    ThermalImageRecordType record;
    const Fw::Time now = this->getTime();
    record.set_timeTag(Fw::TimeValue(now.getTimeBase(), now.getContext(), now.getSeconds(), now.getUSeconds()));

    // Copy the latest streamed frame straight into the record's pixel array
    // (avoids a second 38 KB stack buffer). Requires a prior ENABLE (i.e. camera status is OK)
    U16(&pixels)[LeptonCamera::NUM_PIXELS] = record.get_value();
    char reason[80] = {0};
    const LeptonCamera::Status camStatus =
        camera.getLatestFrame(pixels, LeptonCamera::NUM_PIXELS, CAPTURE_TIMEOUT_MS, reason, sizeof(reason));

    // capture fails
    if (LeptonCamera::OK != camStatus) {
        Fw::LogStringArg reasonArg(reason);
        this->log_WARNING_HI_ImageCaptureFailed(reasonArg);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    // Allocate a data-product container large enough for one image record.
    const FwSizeType dpSize =
        ThermalImageRecordType::SERIALIZED_SIZE + static_cast<FwSizeType>(sizeof(FwDpIdType));
    DpContainer container;

    // TODO: this will throw a NoBuffsAvailable exception if dpBufferStoreSize is too small (default is 10000).
    // right now the fix is to update dpBufferStoreSize in
    // lib/fprime/Svc/Subtopologies/DataProducts/DataProductsConfig/DataProductsConfig.fpp to 48000
    // eventually we should implement a more robust solution as git submodule update will overwrite
    // the config file and we will have to remember to change it back
    const Fw::Success status = this->dpGet_ThermalImageContainer(dpSize, container);

    // If the container allocation fails, log a warning and return an execution error.
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
