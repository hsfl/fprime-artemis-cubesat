// ======================================================================
// \title  testLeptonCamera.hpp
// \author samanthamallari
// \brief  Standalone (F'-free) copy of the LeptonCamera wrapper, so the libuvc
//         streaming path can be compiled + run natively on the Pi without the
//         F' framework. Logic is a 1:1 port of
//         Components/PayloadAdapter_Lepton/LeptonCamera.hpp so fixes proven here
//         copy straight back.
//
// Differences from the real header (the ONLY ones):
//   * U16/U32        -> <cstdint> typedefs instead of <Fw/FPrimeBasicTypes.hpp>
//   * Os::Mutex      -> std::mutex
//   * libuvc handles -> always present (no LEPTON_USE_LIBUVC gate; this harness
//     is libuvc-only by definition)
// ======================================================================

#ifndef TEST_LEPTON_CAMERA_HPP
#define TEST_LEPTON_CAMERA_HPP

#include <cstdint>
#include <mutex>

// Keep the F' primitive names so the implementation stays a literal port.
typedef uint16_t U16;
typedef uint32_t U32;

namespace Components {

class LeptonCamera final {
  public:
    // Lepton 3.x native resolution.
    static constexpr U32 WIDTH = 160;
    static constexpr U32 HEIGHT = 120;
    static constexpr U32 NUM_PIXELS = WIDTH * HEIGHT;  // 19,200

    enum Status {
        OK,               //!< camera is streaming and a valid frame is available
        LIBUVC_ERROR,     //!< libuvc init/find/open/stream configuration failed
        STREAM_NOT_READY, //!< getLatestFrame() called before a successful open()
        FRAME_TIMEOUT     //!< No valid (non-FFC) frame became available in time
    };

    LeptonCamera();
    ~LeptonCamera();

    LeptonCamera(const LeptonCamera&) = delete;
    LeptonCamera& operator=(const LeptonCamera&) = delete;

    //! Bring the camera up and start the continuous Y16 stream. Idempotent.
    Status open(char* reason, U32 reasonSize);

    //! Copy the most recent valid frame into out (NUM_PIXELS U16, little-endian).
    Status getLatestFrame(U16* out, U32 numPixels, U32 timeoutMs, char* reason, U32 reasonSize);

    //! Stop streaming and release the camera. Safe to call when not streaming.
    void close();

    //! @return true if the camera is currently streaming
    bool isStreaming() const;

    //! Internal: invoked from the libuvc streaming thread for each raw frame.
    void ingestFrameRaw(const void* data, U32 numPixels);

  private:
    bool m_streaming;            //!< true between a successful open() and close()
    mutable std::mutex m_mutex;  //!< guards m_haveValidFrame / m_latestFrame
    bool m_haveValidFrame;       //!< true once at least one valid frame is buffered
    U16 m_latestFrame[NUM_PIXELS];

    // Opaque libuvc handles (real targets are uvc_context_t*, etc.). Kept as
    // void* so this header never needs <libuvc/libuvc.h>.
    void* m_ctx;
    void* m_dev;
    void* m_devh;
    void* m_strmh;
    void* m_ctrl;  //!< heap-allocated uvc_stream_ctrl_t
};

}  // namespace Components

#endif
