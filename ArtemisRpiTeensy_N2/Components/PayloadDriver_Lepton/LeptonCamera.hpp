#ifndef Components_PayloadDriver_Lepton_LeptonCamera_HPP
#define Components_PayloadDriver_Lepton_LeptonCamera_HPP

#include <Fw/FPrimeBasicTypes.hpp>

namespace Components {

class LeptonCamera final {
  public:
    static constexpr U32 WIDTH = 160;
    static constexpr U32 HEIGHT = 120;
    static constexpr U32 NUM_PIXELS = WIDTH * HEIGHT;

    enum Status {
        OK,
        DEVICE_ERROR,
        STREAM_NOT_READY,
        FRAME_TIMEOUT,
    };

    LeptonCamera();
    ~LeptonCamera();

    LeptonCamera(const LeptonCamera&) = delete;
    LeptonCamera& operator=(const LeptonCamera&) = delete;

    Status open(char* reason, U32 reasonSize);
    Status getLatestFrame(U16* out, U32 numPixels, U32 timeoutMs, char* reason, U32 reasonSize);
    void close();
    bool isStreaming() const;
    void ingestFrameRaw(const void* data, U32 numPixels);

  private:
    static void writeReason(char* reason, U32 reasonSize, const char* message);
    void fillSyntheticFrame();

    bool m_streaming;
    U32 m_frameCounter;
    U16 m_latestFrame[NUM_PIXELS];
};

}  // namespace Components

#endif
