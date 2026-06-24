// ======================================================================
// \title  LeptonCamera.hpp
// \author samanthamallari
// \brief  Thin libuvc wrapper for the FLIR Lepton thermal camera.
//
// Persistent-streaming model: open() brings the camera up once and starts a
// continuous Y16 stream; a libuvc callback keeps the most recent valid frame
// in a mutex-guarded buffer; getLatestFrame() copies it out cheaply. This is
// the shared substrate for single capture (now) and burst / video / livestream
// (later) -- all of which are just consumers of the latest-frame buffer.
//
// Two build variants share this exact header:
//   * Real libuvc path  (compiled when CMake's find_library(uvc) succeeds,
//     i.e. on the Raspberry Pi target)            -> #ifdef LEPTON_USE_LIBUVC
//   * Deterministic ramp stub (macOS / native dev builds, keeps the
//     data-product path testable without hardware) -> #else
//
// The header is intentionally free of any libuvc types so it compiles
// identically on hosts without libuvc installed.
// ======================================================================

#ifndef Components_PayloadAdapter_Lepton_LeptonCamera_HPP
#define Components_PayloadAdapter_Lepton_LeptonCamera_HPP

#include <Fw/FPrimeBasicTypes.hpp>
#include <Os/Mutex.hpp>

namespace Components {

class LeptonCamera final {
  public:
    // Lepton 3.x native resolution. For a Lepton 2.x (80x60) change these AND
    // the ThermalImageRecordType array bound in PayloadAdapter_Lepton.fpp.
    static constexpr U32 WIDTH = 160;
    static constexpr U32 HEIGHT = 120;
    static constexpr U32 NUM_PIXELS = WIDTH * HEIGHT;  // 19,200

    enum Status {
        OK,             //!< Operation succeeded
        DEVICE_ERROR,   //!< libuvc init/find/open/stream configuration failed
        NOT_STREAMING,  //!< getLatestFrame() called before a successful open()
        FRAME_TIMEOUT   //!< No valid (non-FFC) frame became available in time
    };

    LeptonCamera();
    ~LeptonCamera();

    //! Owns libuvc handles and a mutex: non-copyable.
    LeptonCamera(const LeptonCamera&) = delete;
    LeptonCamera& operator=(const LeptonCamera&) = delete;

    //! Bring the camera up and start the continuous Y16 stream.
    //! Idempotent: a second call while already streaming returns OK.
    //! @param reason      buffer to receive a short failure reason (NUL-terminated)
    //! @param reasonSize  size of reason buffer in bytes
    Status open(char* reason, U32 reasonSize);

    //! Copy the most recent valid frame into out (NUM_PIXELS U16, little-endian).
    //! Returns immediately if a fresh frame is already buffered; otherwise waits
    //! up to timeoutMs for one (covers the Flat-Field-Correction settle right
    //! after open()). Cheap to call repeatedly -- this is what burst / video /
    //! livestream consumers will use.
    //! @param out         destination buffer, must hold at least numPixels U16
    //! @param numPixels   capacity of out; must equal NUM_PIXELS
    //! @param timeoutMs   max time to wait for a frame to be available
    Status getLatestFrame(U16* out, U32 numPixels, U32 timeoutMs, char* reason, U32 reasonSize);

    //! Stop streaming and release the camera. Safe to call when not streaming.
    void close();

    //! @return true if the camera is currently streaming
    bool isStreaming() const;

    //! Internal: invoked from the libuvc streaming thread for each raw frame.
    //! Validates (skips FFC frames) and, if good, copies into the latest-frame
    //! buffer under the mutex. Public only so the file-local C callback in the
    //! .cpp can reach it; not part of the intended user API.
    void ingestFrameRaw(const void* data, U32 numPixels);

  private:
    bool m_streaming;            //!< true between a successful open() and close()
    mutable Os::Mutex m_mutex;   //!< guards m_haveValidFrame / m_latestFrame
    bool m_haveValidFrame;       //!< true once at least one valid frame is buffered
    U16 m_latestFrame[NUM_PIXELS];

#ifdef LEPTON_USE_LIBUVC
    // Opaque libuvc handles (real void* targets are uvc_context_t*, etc.). Kept
    // as void* so this header never needs <libuvc/libuvc.h>.
    void* m_ctx;
    void* m_dev;
    void* m_devh;
    void* m_strmh;
    void* m_ctrl;  //!< heap-allocated uvc_stream_ctrl_t
#endif
};

}  // namespace Components

#endif