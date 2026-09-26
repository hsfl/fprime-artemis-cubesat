// ======================================================================
// \title  LeptonCamera.hpp
// \brief  Thin libuvc wrapper for the FLIR Lepton thermal camera.
//
// This is ported from EPSCOR_C3M_REFACTOR's PayloadAdapter_Lepton camera
// backend into the current PayloadDriver_Lepton component. The real libuvc
// path stays isolated behind LEPTON_USE_LIBUVC so laptop local emulation can
// build and run without camera hardware.
// ======================================================================

#ifndef Components_PayloadDriver_Lepton_LeptonCamera_HPP
#define Components_PayloadDriver_Lepton_LeptonCamera_HPP

#include <Fw/FPrimeBasicTypes.hpp>
#include <Os/Mutex.hpp>

namespace Components {

class LeptonCamera final {
  public:
    static constexpr U32 WIDTH = 160;
    static constexpr U32 HEIGHT = 120;
    static constexpr U32 NUM_PIXELS = WIDTH * HEIGHT;

    enum Status {
        OK,
        LIBUVC_ERROR,
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
    const char* backendName() const;
    void ingestFrameRaw(const void* data, U32 numPixels);

  private:
    enum Backend {
        BACKEND_AUTO,
        BACKEND_UVC,
        BACKEND_SAMPLE,
        BACKEND_SYNTHETIC,
    };

    static Backend parseBackend(const char* value, bool& valid);
    static const char* backendName(Backend backend);
    static void writeReason(char* reason, U32 reasonSize, const char* message);
    static bool isValidFrame(const U16* pixels, U32 numPixels);

    Status openAuto(char* reason, U32 reasonSize);
    Status openUvc(char* reason, U32 reasonSize);
    Status openSample(char* reason, U32 reasonSize);
    Status openSynthetic(char* reason, U32 reasonSize);
    bool loadSampleFrame(char* reason, U32 reasonSize);
    bool loadSampleCsv(const char* path);
    void fillSyntheticFrame();

    bool m_streaming;
    Backend m_activeBackend;
    U32 m_frameCounter;
    mutable Os::Mutex m_mutex;
    bool m_haveValidFrame;
    U16 m_latestFrame[NUM_PIXELS];

#ifdef LEPTON_USE_LIBUVC
    void* m_ctx;
    void* m_dev;
    void* m_devh;
    void* m_strmh;
    void* m_ctrl;
#endif
};

}  // namespace Components

#endif
