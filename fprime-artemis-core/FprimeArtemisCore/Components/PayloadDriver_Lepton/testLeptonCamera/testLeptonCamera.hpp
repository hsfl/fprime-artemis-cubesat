// ======================================================================
// \title  testLeptonCamera.hpp
// \brief  Standalone F Prime-free Lepton camera harness.
//
// Ported from EPSCOR_C3M_REFACTOR's PayloadAdapter_Lepton harness. It keeps the
// UVC camera proof independent of topology, GDS, and Data Products.
// ======================================================================

#ifndef TEST_LEPTON_CAMERA_HPP
#define TEST_LEPTON_CAMERA_HPP

#include <cstdint>
#include <mutex>

typedef uint16_t U16;
typedef uint32_t U32;

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
    void ingestFrameRaw(const void* data, U32 numPixels);

  private:
    bool m_streaming;
    mutable std::mutex m_mutex;
    bool m_haveValidFrame;
    U16 m_latestFrame[NUM_PIXELS];
    void* m_ctx;
    void* m_dev;
    void* m_devh;
    void* m_strmh;
    void* m_ctrl;
};

}  // namespace Components

#endif
