// ======================================================================
// \title  BosonCamera.hpp
// \brief  Small raw-frame helper for the 320x256 Boson camera.
// ======================================================================

#ifndef Components_PayloadDriver_Boson_BosonCamera_HPP
#define Components_PayloadDriver_Boson_BosonCamera_HPP

#include <Fw/FPrimeBasicTypes.hpp>

namespace Components {

class BosonCamera final {
  public:
    static constexpr U32 WIDTH = 320U;
    static constexpr U32 HEIGHT = 256U;
    static constexpr U32 TELEMETRY_ROWS = 2U;
    static constexpr U32 NUM_PIXELS = WIDTH * HEIGHT;

    enum Status {
        OK,
        DEVICE_ERROR,
        FORMAT_ERROR,
        STREAM_NOT_READY,
        FRAME_TIMEOUT,
        SAMPLE_ERROR,
        INVALID_BACKEND,
        INVALID_ARGUMENT,
    };

    BosonCamera();
    ~BosonCamera();

    BosonCamera(const BosonCamera&) = delete;
    BosonCamera& operator=(const BosonCamera&) = delete;

    Status open(char* reason, U32 reasonSize);
    Status getLatestFrame(U16* out, U32 numPixels, U32 timeoutMs, char* reason, U32 reasonSize);
    void close();

    bool isStreaming() const;
    const char* backendName() const;

    // Reject startup/test-pattern frames before they become science products.
    // Public so the hardware-free regression harness can exercise the same
    // acceptance rule used by the V4L2 path.
    static bool isUsableFrame(const U16* pixels, U32 numPixels);

  private:
    enum Backend {
        BACKEND_V4L2,
        BACKEND_SAMPLE,
        BACKEND_SYNTHETIC,
    };

    static Backend defaultBackend();
    static Backend parseBackend(const char* value, bool& valid);
    static const char* backendName(Backend backend);
    static void writeReason(char* reason, U32 reasonSize, const char* message);

    Status openV4l2(char* reason, U32 reasonSize);
    Status openSample(char* reason, U32 reasonSize);
    Status openSynthetic(char* reason, U32 reasonSize);
    bool loadSampleFrame(const char* path);
    void fillSyntheticFrame();

#ifdef __linux__
    Status captureV4l2Frame(U16* out, U32 timeoutMs, char* reason, U32 reasonSize);
    void closeV4l2();
#endif

    bool m_streaming;
    Backend m_activeBackend;
    U16 m_latestFrame[NUM_PIXELS];

#ifdef __linux__
    int m_v4l2Fd;
    void* m_v4l2Buffers;
    U32 m_v4l2BufferCount;
    U32 m_v4l2Stride;
    U32 m_v4l2Height;
    bool m_v4l2StreamOn;
#endif
};

}  // namespace Components

#endif
