#include "Components/PayloadDriver_Lepton/PayloadDriver_Lepton.hpp"

#include "Subtopologies/ArtemisDataProducts/ArtemisDataProductsConfig/FppConstantsAc.hpp"
#include "Fw/Types/FileNameString.hpp"
#include "config/DpCfg.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

namespace Components {

namespace {

constexpr U32 CAPTURE_TIMEOUT_MS = 5000U;
constexpr U32 PREVIEW_WIDTH = 80U;
constexpr U32 PREVIEW_HEIGHT = 60U;
constexpr U16 PREVIEW_MIN_CENTIKELVIN = 27315U;
constexpr U16 PREVIEW_MAX_CENTIKELVIN = 37315U;
constexpr U32 PREVIEW_WINDOW_CENTIKELVIN =
    static_cast<U32>(PREVIEW_MAX_CENTIKELVIN - PREVIEW_MIN_CENTIKELVIN);

}  // namespace

PayloadDriver_Lepton::PayloadDriver_Lepton(const char* const compName)
    : PayloadDriver_LeptonComponentBase(compName),
      m_camera(),
      m_latestPreviewSource{},
      m_previewBuffer{},
      m_lastDurationSeconds(0),
      m_lastProductId(0),
      m_lastDataBytes(0),
      m_lastFileBytes(0),
      m_lastCaptureStatus(CAPTURE_OK),
      m_pendingWrites(0),
      m_pendingProductId(0),
      m_pendingPath() {}

PayloadDriver_Lepton::~PayloadDriver_Lepton() {}

void PayloadDriver_Lepton::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadDriver_Lepton::requestIn_handler(FwIndexType portNum, U32 durationSeconds) {
    static_cast<void>(portNum);
    this->m_lastDurationSeconds = durationSeconds;
    if (!this->captureThermalImage(durationSeconds)) {
        this->writeTelemetry();
    }
}

void PayloadDriver_Lepton::deactivateIn_handler(FwIndexType portNum) {
    static_cast<void>(portNum);
    this->m_camera.close();
}

void PayloadDriver_Lepton::previewRequestIn_handler(FwIndexType portNum) {
    static_cast<void>(portNum);
    (void)this->publishLatestPreview();
}

void PayloadDriver_Lepton::dpWrittenIn_handler(FwIndexType portNum,
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
        this->publishFailure(CAPTURE_CRC_ERROR, "failed to compute Lepton data-product CRC");
        this->writeTelemetry();
        return;
    }

    this->m_lastCaptureStatus = CAPTURE_OK;
    this->log_ACTIVITY_HI_ImageCaptureSuccess(size);
    if (this->isConnected_statusOut_OutputPort(0)) {
        const Fw::String sourcePath(fileName.toChar());
        this->statusOut_out(0,
                             this->m_pendingProductId,
                             this->m_lastFileBytes,
                             Components::ScienceProductSource::REAL_PAYLOAD,
                             sourcePath,
                             sourceCrc);
    }
    this->m_pendingPath.clear();
    this->m_pendingProductId = 0U;
    this->writeTelemetry();
}

bool PayloadDriver_Lepton::captureThermalImage(U32 durationSeconds) {
    static_cast<void>(durationSeconds);
    if (this->m_pendingWrites != 0U) {
        this->publishFailure(CAPTURE_BUSY, "previous Lepton data product write still pending");
        return false;
    }
    if (!this->isConnected_productGetOut_OutputPort(0) || !this->isConnected_productSendOut_OutputPort(0)) {
        this->publishFailure(CAPTURE_DP_NOT_CONNECTED, "data-product ports are not connected");
        return false;
    }

    char reason[96] = {};
    if (!this->ensureCameraOpen(reason, sizeof(reason))) {
        this->publishFailure(CAPTURE_CAMERA_ERROR, reason);
        return false;
    }

    this->log_ACTIVITY_LO_ImageCaptureStart();

    ThermalImageRecordType record;
    const Fw::Time now = this->getTime();
    record.set_timeTag(Fw::TimeValue(now.getTimeBase(), now.getContext(), now.getSeconds(), now.getUSeconds()));

    U16(&pixels)[LeptonCamera::NUM_PIXELS] = record.get_value();
    const LeptonCamera::Status cameraStatus =
        this->m_camera.getLatestFrame(pixels, LeptonCamera::NUM_PIXELS, CAPTURE_TIMEOUT_MS, reason, sizeof(reason));
    if (cameraStatus != LeptonCamera::OK) {
        this->publishFailure(CAPTURE_CAMERA_ERROR, reason);
        return false;
    }

    const FwSizeType dpSize =
        ThermalImageRecordType::SERIALIZED_SIZE + static_cast<FwSizeType>(sizeof(FwDpIdType));
    DpContainer container;
    const Fw::Success status = this->dpGet_ThermalImageContainer(dpSize, container);
    if (status == Fw::Success::FAILURE) {
        this->log_WARNING_HI_DpMemoryFailure(dpSize);
        this->publishFailure(CAPTURE_DP_NO_MEMORY, "data-product buffer allocation failed");
        return false;
    }

    const Fw::SerializeStatus serializeStatus = container.serializeRecord_ThermalImageRecord(record);
    if (serializeStatus != Fw::FW_SERIALIZE_OK) {
        this->publishFailure(CAPTURE_SERIALIZE_ERROR, "thermal image serialization failed");
        return false;
    }

    Fw::FileNameString expectedPath;
    expectedPath.format(DP_FILENAME_FORMAT,
                        ArtemisDataProductsConfig::Paths::dpDir,
                        container.getId(),
                        now.getSeconds(),
                        now.getUSeconds());

    this->m_lastProductId += 1U;
    this->m_pendingProductId = this->m_lastProductId;
    this->m_pendingPath = expectedPath.toChar();
    this->m_pendingWrites = 1U;
    this->m_lastDataBytes = clampSize(dpSize);
    this->m_lastFileBytes = 0U;
    this->m_lastCaptureStatus = CAPTURE_OK;

    this->dpSend(container, now);
    this->log_ACTIVITY_HI_ImageCaptureQueued(dpSize);
    this->writeTelemetry();
    return true;
}

bool PayloadDriver_Lepton::publishLatestPreview() {
    if (!this->isConnected_previewOut_OutputPort(0)) {
        return false;
    }

    char reason[96] = {};
    if (!this->ensureCameraOpen(reason, sizeof(reason))) {
        this->publishFailure(CAPTURE_CAMERA_ERROR, reason);
        this->writeTelemetry();
        return false;
    }

    // getLatestFrame snapshots the camera's validated source while its mutex
    // is held. Conversion happens after that lock has been released, so UVC
    // callbacks can continue publishing the newest source frame.
    const LeptonCamera::Status status = this->m_camera.getLatestFrame(
        this->m_latestPreviewSource,
        LeptonCamera::NUM_PIXELS,
        CAPTURE_TIMEOUT_MS,
        reason,
        sizeof(reason));
    if (status != LeptonCamera::OK) {
        this->publishFailure(CAPTURE_CAMERA_ERROR, reason);
        this->writeTelemetry();
        return false;
    }

    this->downsampleLatestPreview();
    Fw::Buffer preview(this->m_previewBuffer, sizeof(this->m_previewBuffer));
    this->previewOut_out(0, preview);
    return true;
}

void PayloadDriver_Lepton::downsampleLatestPreview() {
    for (U32 y = 0U; y < PREVIEW_HEIGHT; ++y) {
        for (U32 x = 0U; x < PREVIEW_WIDTH; ++x) {
            const U32 sourceX = x * 2U;
            const U32 sourceY = y * 2U;
            const U32 sourceIndex = sourceY * LeptonCamera::WIDTH + sourceX;
            const U32 average =
                (static_cast<U32>(this->m_latestPreviewSource[sourceIndex]) +
                 static_cast<U32>(this->m_latestPreviewSource[sourceIndex + 1U]) +
                 static_cast<U32>(this->m_latestPreviewSource[sourceIndex + LeptonCamera::WIDTH]) +
                 static_cast<U32>(this->m_latestPreviewSource[sourceIndex + LeptonCamera::WIDTH + 1U]) +
                 2U) /
                4U;
            U8 value = 0U;
            if (average >= PREVIEW_MAX_CENTIKELVIN) {
                value = 255U;
            } else if (average > PREVIEW_MIN_CENTIKELVIN) {
                const U32 scaled = (average - PREVIEW_MIN_CENTIKELVIN) * 255U;
                value = static_cast<U8>((scaled + (PREVIEW_WINDOW_CENTIKELVIN / 2U)) /
                                        PREVIEW_WINDOW_CENTIKELVIN);
            }
            this->m_previewBuffer[y * PREVIEW_WIDTH + x] = value;
        }
    }
}

bool PayloadDriver_Lepton::ensureCameraOpen(char* reason, U32 reasonSize) {
    if (this->m_camera.isStreaming()) {
        return true;
    }
    return this->m_camera.open(reason, reasonSize) == LeptonCamera::OK;
}

void PayloadDriver_Lepton::publishFailure(CaptureStatus status, const char* reason) {
    this->m_lastCaptureStatus = status;
    this->log_WARNING_HI_ImageCaptureFailed(Fw::LogStringArg(reason));
    if (this->isConnected_statusOut_OutputPort(0)) {
        const Fw::String emptyPath("");
        this->statusOut_out(0, 0U, 0U, Components::ScienceProductSource::UNKNOWN, emptyPath, 0U);
    }
}

void PayloadDriver_Lepton::writeTelemetry() {
    this->tlmWrite_LastDurationSeconds(this->m_lastDurationSeconds);
    this->tlmWrite_LastProductId(this->m_lastProductId);
    this->tlmWrite_LastDataBytes(this->m_lastDataBytes);
    this->tlmWrite_LastFileBytes(this->m_lastFileBytes);
    this->tlmWrite_LastCaptureStatus(this->m_lastCaptureStatus);
    this->tlmWrite_PendingWrites(this->m_pendingWrites);
}

U32 PayloadDriver_Lepton::clampSize(FwSizeType size) {
    if (size > static_cast<FwSizeType>(std::numeric_limits<U32>::max())) {
        return std::numeric_limits<U32>::max();
    }
    return static_cast<U32>(size);
}

bool PayloadDriver_Lepton::computeFileCrc16(const std::string& outputPath, U32& crcOut) {
    FILE* file = std::fopen(outputPath.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    U16 crc = 0xFFFFU;
    int value = 0;
    while ((value = std::fgetc(file)) != EOF) {
        crc ^= static_cast<U16>(static_cast<U8>(value)) << 8U;
        for (U8 bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0) {
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
