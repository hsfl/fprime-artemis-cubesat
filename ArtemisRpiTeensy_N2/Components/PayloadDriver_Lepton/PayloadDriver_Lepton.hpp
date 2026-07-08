#ifndef Components_PayloadDriver_Lepton_HPP
#define Components_PayloadDriver_Lepton_HPP

#include "Components/PayloadDriver_Lepton/LeptonCamera.hpp"
#include "Components/PayloadDriver_Lepton/PayloadDriver_LeptonComponentAc.hpp"

#include <string>

namespace Components {

class PayloadDriver_Lepton final : public PayloadDriver_LeptonComponentBase {
  public:
    PayloadDriver_Lepton(const char* const compName);
    ~PayloadDriver_Lepton();

  private:
    enum CaptureStatus : U32 {
        CAPTURE_OK = 0,
        CAPTURE_BUSY = 1,
        CAPTURE_CAMERA_ERROR = 2,
        CAPTURE_DP_NOT_CONNECTED = 3,
        CAPTURE_DP_NO_MEMORY = 4,
        CAPTURE_SERIALIZE_ERROR = 5,
        CAPTURE_WRITE_MISMATCH = 6,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 durationSeconds) override;
    void dpWrittenIn_handler(FwIndexType portNum,
                             const Fw::StringBase& fileName,
                             FwDpPriorityType priority,
                             FwSizeType size) override;
    void ENABLE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void DISABLE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void CAPTURE_IMAGE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    bool captureThermalImage(U32 durationSeconds);
    bool ensureCameraOpen(char* reason, U32 reasonSize);
    void publishFailure(CaptureStatus status, const char* reason);
    void writeTelemetry();
    static U32 clampSize(FwSizeType size);

    LeptonCamera m_camera;
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
