#ifndef Components_PayloadDriver_Boson_HPP
#define Components_PayloadDriver_Boson_HPP

#include "Components/PayloadDriver_Boson/BosonCamera.hpp"
#include "Components/PayloadDriver_Boson/PayloadDriver_BosonComponentAc.hpp"

#include <string>

namespace Components {

class PayloadDriver_Boson final : public PayloadDriver_BosonComponentBase {
  public:
    PayloadDriver_Boson(const char* const compName);
    ~PayloadDriver_Boson();

  private:
    enum CaptureStatus : U32 {
        CAPTURE_OK = 0,
        CAPTURE_BUSY = 1,
        CAPTURE_CAMERA_ERROR = 2,
        CAPTURE_DP_NOT_CONNECTED = 3,
        CAPTURE_DP_NO_MEMORY = 4,
        CAPTURE_SERIALIZE_ERROR = 5,
        CAPTURE_WRITE_MISMATCH = 6,
        CAPTURE_CRC_ERROR = 7,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 durationSeconds) override;
    void deactivateIn_handler(FwIndexType portNum) override;
    void dpWrittenIn_handler(FwIndexType portNum,
                             const Fw::StringBase& fileName,
                             FwDpPriorityType priority,
                             FwSizeType size) override;

    bool captureImage(U32 durationSeconds);
    bool ensureCameraOpen(char* reason, U32 reasonSize);
    void publishFailure(CaptureStatus status, const char* reason);
    void writeTelemetry();

    static bool computeFileCrc16(const std::string& outputPath, U32& crcOut);
    static U32 clampSize(FwSizeType size);

    BosonCamera m_camera;
    U32 m_lastDurationSeconds;
    U32 m_lastProductId;
    U32 m_lastDataBytes;
    U32 m_lastFileBytes;
    U32 m_lastCaptureStatus;
    U32 m_pendingWrites;
    U32 m_pendingProductId;
    std::string m_pendingPath;
};

}  // namespace Components

#endif
