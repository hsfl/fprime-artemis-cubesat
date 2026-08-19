#include "Components/PayloadDriver_Boson/PayloadDriver_Boson.hpp"

#include "Fw/Types/FileNameString.hpp"
#include "Subtopologies/ArtemisDataProducts/ArtemisDataProductsConfig/FppConstantsAc.hpp"
#include "config/DpCfg.hpp"

#include <cstdio>
#include <limits>

namespace Components {

namespace {

constexpr U32 CAPTURE_TIMEOUT_MS = 5000U;

}  // namespace

PayloadDriver_Boson::PayloadDriver_Boson(const char* const compName)
    : PayloadDriver_BosonComponentBase(compName),
      m_camera(),
      m_lastDurationSeconds(0U),
      m_lastProductId(0U),
      m_lastDataBytes(0U),
      m_lastFileBytes(0U),
      m_lastCaptureStatus(CAPTURE_OK),
      m_pendingWrites(0U),
      m_pendingProductId(0U),
      m_pendingPath() {}

PayloadDriver_Boson::~PayloadDriver_Boson() {}

void PayloadDriver_Boson::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadDriver_Boson::requestIn_handler(FwIndexType portNum, U32 durationSeconds) {
    static_cast<void>(portNum);
    this->m_lastDurationSeconds = durationSeconds;
    if (!this->captureImage(durationSeconds)) {
        this->writeTelemetry();
    }
}

void PayloadDriver_Boson::deactivateIn_handler(FwIndexType portNum) {
    static_cast<void>(portNum);
    this->m_camera.close();
}

void PayloadDriver_Boson::dpWrittenIn_handler(FwIndexType portNum,
                                               const Fw::StringBase& fileName,
                                               FwDpPriorityType priority,
                                               FwSizeType size) {
    static_cast<void>(portNum);
    static_cast<void>(priority);
    if (this->m_pendingWrites == 0U) {
        return;
    }

    if (this->m_pendingPath != fileName.toChar()) {
        this->m_lastCaptureStatus = CAPTURE_WRITE_MISMATCH;
        this->writeTelemetry();
        return;
    }

    this->m_pendingWrites -= 1U;
    this->m_lastFileBytes = clampSize(size);

    U32 sourceCrc = 0U;
    if (!computeFileCrc16(fileName.toChar(), sourceCrc)) {
        this->m_pendingPath.clear();
        this->m_pendingProductId = 0U;
        this->publishFailure(CAPTURE_CRC_ERROR, "failed to compute Boson data-product CRC");
        this->writeTelemetry();
        return;
    }

    this->m_lastCaptureStatus = CAPTURE_OK;
    this->log_ACTIVITY_HI_ImageCaptureSuccess(this->m_pendingProductId, size);
    if (this->isConnected_statusOut_OutputPort(0)) {
        const Fw::String sourcePath(fileName.toChar());
        this->statusOut_out(
            0,
            this->m_pendingProductId,
            this->m_lastFileBytes,
            Components::ScienceProductSource::BOSON,
            sourcePath,
            sourceCrc
        );
    }
    this->m_pendingPath.clear();
    this->m_pendingProductId = 0U;
    this->writeTelemetry();
}

bool PayloadDriver_Boson::captureImage(U32 durationSeconds) {
    static_cast<void>(durationSeconds);
    if (this->m_pendingWrites != 0U) {
        this->publishFailure(CAPTURE_BUSY, "previous Boson data-product write is still pending");
        return false;
    }
    if (!this->isConnected_productGetOut_OutputPort(0) ||
        !this->isConnected_productSendOut_OutputPort(0)) {
        this->publishFailure(CAPTURE_DP_NOT_CONNECTED, "data-product ports are not connected");
        return false;
    }

    char reason[96] = {};
    if (!this->ensureCameraOpen(reason, sizeof(reason))) {
        this->publishFailure(CAPTURE_CAMERA_ERROR, reason);
        return false;
    }

    const U32 captureId = this->m_lastProductId + 1U;
    this->log_ACTIVITY_LO_ImageCaptureStart(captureId);

    BosonImageRecordType record;
    const Fw::Time now = this->getTime();
    record.set_timeTag(
        Fw::TimeValue(now.getTimeBase(), now.getContext(), now.getSeconds(), now.getUSeconds())
    );

    U16 (&pixels)[BosonCamera::NUM_PIXELS] = record.get_value();
    const BosonCamera::Status cameraStatus = this->m_camera.getLatestFrame(
        pixels,
        BosonCamera::NUM_PIXELS,
        CAPTURE_TIMEOUT_MS,
        reason,
        sizeof(reason)
    );
    if (cameraStatus != BosonCamera::OK) {
        this->publishFailure(CAPTURE_CAMERA_ERROR, reason);
        return false;
    }

    const FwSizeType dpSize =
        BosonImageRecordType::SERIALIZED_SIZE + static_cast<FwSizeType>(sizeof(FwDpIdType));
    DpContainer container;
    const Fw::Success status = this->dpGet_BosonImageContainer(dpSize, container);
    if (status == Fw::Success::FAILURE) {
        this->log_WARNING_HI_DpMemoryFailure(dpSize);
        this->publishFailure(CAPTURE_DP_NO_MEMORY, "data-product buffer allocation failed");
        return false;
    }

    const Fw::SerializeStatus serializeStatus =
        container.serializeRecord_BosonImageRecord(record);
    if (serializeStatus != Fw::FW_SERIALIZE_OK) {
        this->publishFailure(CAPTURE_SERIALIZE_ERROR, "Boson image serialization failed");
        return false;
    }

    Fw::FileNameString expectedPath;
    expectedPath.format(
        DP_FILENAME_FORMAT,
        ArtemisDataProductsConfig::Paths::dpDir,
        container.getId(),
        now.getSeconds(),
        now.getUSeconds()
    );

    this->m_lastProductId = captureId;
    this->m_pendingProductId = captureId;
    this->m_pendingPath = expectedPath.toChar();
    this->m_pendingWrites = 1U;
    this->m_lastDataBytes = clampSize(dpSize);
    this->m_lastFileBytes = 0U;
    this->m_lastCaptureStatus = CAPTURE_OK;

    this->dpSend(container, now);
    this->log_ACTIVITY_HI_ImageCaptureQueued(captureId, dpSize);
    this->writeTelemetry();
    return true;
}

bool PayloadDriver_Boson::ensureCameraOpen(char* reason, U32 reasonSize) {
    if (this->m_camera.isStreaming()) {
        return true;
    }
    const BosonCamera::Status status = this->m_camera.open(reason, reasonSize);
    if (status == BosonCamera::OK) {
        this->log_ACTIVITY_LO_BosonBackendSelected(Fw::LogStringArg(this->m_camera.backendName()));
    }
    return status == BosonCamera::OK;
}

void PayloadDriver_Boson::publishFailure(CaptureStatus status, const char* reason) {
    this->m_lastCaptureStatus = status;
    this->log_WARNING_HI_ImageCaptureFailed(Fw::LogStringArg(reason));
    if (this->isConnected_statusOut_OutputPort(0)) {
        const Fw::String emptyPath("");
        this->statusOut_out(
            0,
            0U,
            0U,
            Components::ScienceProductSource::UNKNOWN,
            emptyPath,
            0U
        );
    }
}

void PayloadDriver_Boson::writeTelemetry() {
    this->tlmWrite_LastDurationSeconds(this->m_lastDurationSeconds);
    this->tlmWrite_LastProductId(this->m_lastProductId);
    this->tlmWrite_LastDataBytes(this->m_lastDataBytes);
    this->tlmWrite_LastFileBytes(this->m_lastFileBytes);
    this->tlmWrite_LastCaptureStatus(this->m_lastCaptureStatus);
    this->tlmWrite_CaptureActive(this->m_pendingWrites != 0U ? 1U : 0U);
}

U32 PayloadDriver_Boson::clampSize(FwSizeType size) {
    if (size > static_cast<FwSizeType>(std::numeric_limits<U32>::max())) {
        return std::numeric_limits<U32>::max();
    }
    return static_cast<U32>(size);
}

bool PayloadDriver_Boson::computeFileCrc16(const std::string& outputPath, U32& crcOut) {
    FILE* const file = std::fopen(outputPath.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    U16 crc = 0xFFFFU;
    int value = 0;
    while ((value = std::fgetc(file)) != EOF) {
        crc ^= static_cast<U16>(static_cast<U8>(value)) << 8U;
        for (U8 bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<U16>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<U16>(crc << 1U);
            }
        }
    }
    (void)std::fclose(file);
    crcOut = static_cast<U32>(crc);
    return true;
}

}  // namespace Components
