// ======================================================================
// \title  LeptonCamera.cpp
// \author samanthamallari
// \brief  libuvc wrapper for the FLIR Lepton (real path) + ramp stub.
//
// LEPTON_USE_LIBUVC is defined by CMake only when find_library(uvc) succeeds
// (Raspberry Pi target). On hosts without libuvc (macOS / native dev) the stub
// path compiles instead, so the data-product pipeline stays buildable and
// testable without camera hardware.
// ======================================================================

#include "Components/PayloadAdapter_Lepton/LeptonCamera.hpp"

#include <Fw/Time/TimeInterval.hpp>
#include <Os/Task.hpp>

#include <cstdio>
#include <cstring>

namespace {

// FFC (Flat-Field Correction) frames emitted right after the camera opens are
// nearly uniform/blank. A frame is valid when fewer than 80% of its pixels read
// below 1000.
bool isValidFrame(const U16* px, U32 numPixels) {
    U32 inValidCount = 0;
    for (U32 i = 0; i < numPixels; i++) { // count invalid pixels
        if (px[i] < 1000) {
            inValidCount++;
        }
    }
    return inValidCount < ((numPixels * 4) / 5);
}

void writeReason(char* reason, U32 reasonSize, const char* msg) {
    if (reason != nullptr && reasonSize > 0) {
        std::snprintf(reason, reasonSize, "%s", msg);
    }
}

}  // namespace

namespace Components {

// ----------------------------------------------------------------------
// Construction / destruction (shared)
// ----------------------------------------------------------------------

LeptonCamera::LeptonCamera() : m_streaming(false), m_haveValidFrame(false) {
    std::memset(m_latestFrame, 0, sizeof(m_latestFrame));
#ifdef LEPTON_USE_LIBUVC
    m_ctx = nullptr;
    m_dev = nullptr;
    m_devh = nullptr;
    m_strmh = nullptr;
    m_ctrl = nullptr;
#endif
}

LeptonCamera::~LeptonCamera() {
    this->close();
}

bool LeptonCamera::isStreaming() const {
    return m_streaming;
}

// ----------------------------------------------------------------------
// Latest-frame buffer access (shared by real + stub)
// ----------------------------------------------------------------------

void LeptonCamera::ingestFrameRaw(const void* data, U32 numPixels) {
    if (data == nullptr || numPixels != NUM_PIXELS) {
        return;
    }
    const U16* px = static_cast<const U16*>(data);
    if (!isValidFrame(px, numPixels)) {
        return;  // drop FFC / blank frames; keep the last good one
    }
    Os::ScopeLock lock(m_mutex);
    std::memcpy(m_latestFrame, px, numPixels * sizeof(U16));
    m_haveValidFrame = true;
}

//! Copy the most recent valid frame into out (NUM_PIXELS U16, little-endian).
//! Returns immediately if a fresh frame is already buffered; otherwise waits
//! up to timeoutMs for one (covers the Flat-Field-Correction settle right
//! after open()).
//! @param out         destination buffer, must hold at least numPixels U16
//! @param numPixels   capacity of out; must equal NUM_PIXELS
//! @param timeoutMs   max time to wait for a frame to be available
LeptonCamera::Status LeptonCamera::getLatestFrame(U16* out,
                                                  U32 numPixels,
                                                  U32 timeoutMs,
                                                  char* reason,
                                                  U32 reasonSize) {
    if (!m_streaming) {
        writeReason(reason, reasonSize, "camera not streaming (ENABLE first)");
        return STREAM_NOT_READY;
    }

    if (out == nullptr) {
        writeReason(reason, reasonSize, "null output buffer");
        return LIBUVC_ERROR;
    }

    if (numPixels != NUM_PIXELS) {
        writeReason(reason, reasonSize, "frame buffer size mismatch");
        return LIBUVC_ERROR;
    }

    // Poll the latest-frame buffer until a valid frame exists or we time out.
    // The wait covers the FFC settle right after open().
    const U32 pollMs = 10;
    U32 waitedMs = 0;
    for (;;) { // loop until we get a valid frame or time out
        {
            Os::ScopeLock lock(m_mutex);
            if (m_haveValidFrame) {
                std::memcpy(out, m_latestFrame, numPixels * sizeof(U16));
                return OK;
            }
        }
        if (waitedMs >= timeoutMs) {
            writeReason(reason, reasonSize, "timed out waiting for a valid frame");
            return FRAME_TIMEOUT;
        }
        (void)Os::Task::delay(Fw::TimeInterval(0, pollMs * 1000));
        waitedMs += pollMs;
    }
}

}  // namespace Components

// ======================================================================
// Real libuvc implementation
// ======================================================================
// #ifdef LEPTON_USE_LIBUVC

#include <libuvc/libuvc.h>

namespace {

// libuvc streaming-thread callback: hand each frame to the owning LeptonCamera.
// Must never throw across the C boundary.
void leptonFrameCallback(uvc_frame_t* frame, void* userPtr) {
    if (frame == nullptr || userPtr == nullptr || frame->data == nullptr) {
        return;
    }
    Components::LeptonCamera* self = static_cast<Components::LeptonCamera*>(userPtr);
    self->ingestFrameRaw(frame->data, static_cast<U32>(frame->width * frame->height));
}

// Enumerate the device's modes (fourcc + WxH, space-separated, truncated to the
// buffer) for self-diagnosing failure events -- no redeploy needed to see what
// the camera actually offers.
void describeModes(uvc_device_handle_t* devh, char* out, U32 outSize) {
    out[0] = '\0';
    int off = 0;
    for (const uvc_format_desc_t* fmt = uvc_get_format_descs(devh);
         fmt != nullptr && off < static_cast<int>(outSize) - 1; fmt = fmt->next) {
        for (const uvc_frame_desc_t* frame = fmt->frame_descs;
             frame != nullptr && off < static_cast<int>(outSize) - 1; frame = frame->next) {
            const int n = std::snprintf(out + off, outSize - static_cast<U32>(off), "%.4s%ux%u ",
                                        reinterpret_cast<const char*>(fmt->fourccFormat),
                                        static_cast<unsigned>(frame->wWidth),
                                        static_cast<unsigned>(frame->wHeight));
            if (n < 0) {
                break;
            }
            off += n;
        }
    }
}

// Build a stream control for the Lepton's native Y16 (16-bit greyscale) format
// at width x height, returning a libuvc error code (UVC_SUCCESS on success).
//
// We can't use uvc_get_stream_ctrl_format_size() directly for Y16: its GUID->enum
// lookup fails on libuvc builds whose format table doesn't recognize the "Y16 "
// GUID. There the Lepton's format parses as UVC_FRAME_FORMAT_UNKNOWN, so a
// UVC_FRAME_FORMAT_GRAY16 request matches nothing -> UVC_ERROR_INVALID_MODE.
//
// Strategy: (1) find the Y16 format/frame descriptors by their raw fourcc bytes;
// (2) bootstrap a valid control -- crucially ctrl->bInterfaceNumber, which we
// can't read from the public API -- by calling uvc_get_stream_ctrl_format_size
// for a SIBLING format on the same VideoStreaming interface that libuvc's table
// DOES recognize (the Lepton also exposes UYVY/GRAY8 at the same size); (3)
// repoint the control at the Y16 format/frame indices and renegotiate via
// uvc_probe_stream_ctrl. On any failure a diagnostic (including the modes the
// device actually exposes) is written to reason.
uvc_error_t probeY16StreamCtrl(uvc_device_handle_t* devh,
                               uvc_stream_ctrl_t* ctrl,
                               U32 width,
                               U32 height,
                               char* reason,
                               U32 reasonSize) {
    // (1) Locate the Y16 format + frame by raw fourcc (the GUID->enum lookup is broken).
    const uvc_format_desc_t* y16Fmt = nullptr;
    const uvc_frame_desc_t* y16Frame = nullptr;
    for (const uvc_format_desc_t* fmt = uvc_get_format_descs(devh); fmt != nullptr && y16Frame == nullptr;
         fmt = fmt->next) {
        if (std::memcmp(fmt->fourccFormat, "Y16 ", 4) != 0) {
            continue;
        }
        for (const uvc_frame_desc_t* frame = fmt->frame_descs; frame != nullptr; frame = frame->next) {
            if (frame->wWidth == width && frame->wHeight == height) {
                y16Fmt = fmt;
                y16Frame = frame;
                break;
            }
        }
    }
    if (y16Frame == nullptr) {
        char modes[96];
        describeModes(devh, modes, sizeof(modes));
        std::snprintf(reason, reasonSize, "no Y16 %ux%u; have: %s",
                      static_cast<unsigned>(width), static_cast<unsigned>(height), modes);
        return UVC_ERROR_INVALID_MODE;
    }

    // (2) Bootstrap ctrl (notably bInterfaceNumber) via a recognized sibling format
    //     on the same VideoStreaming interface, derived fps from the Y16 frame.
    const unsigned fps = (y16Frame->dwDefaultFrameInterval != 0)
                             ? static_cast<unsigned>(10000000UL / y16Frame->dwDefaultFrameInterval)
                             : 9;
    const uvc_frame_format kKnown[] = {UVC_FRAME_FORMAT_UYVY, UVC_FRAME_FORMAT_GRAY8,
                                       UVC_FRAME_FORMAT_YUYV};
    uvc_error_t res = UVC_ERROR_INVALID_MODE;
    for (const uvc_frame_format kf : kKnown) {
        res = uvc_get_stream_ctrl_format_size(devh, ctrl, kf, static_cast<int>(width),
                                              static_cast<int>(height), static_cast<int>(fps));
        if (res == UVC_SUCCESS) {
            break;
        }
    }
    if (res < 0) {
        char modes[96];
        describeModes(devh, modes, sizeof(modes));
        std::snprintf(reason, reasonSize, "no sibling format to bind VS interface; have: %s", modes);
        return res;
    }

    // (3) Repoint the negotiated control at the Y16 format/frame and renegotiate.
    ctrl->bFormatIndex = y16Fmt->bFormatIndex;
    ctrl->bFrameIndex = y16Frame->bFrameIndex;
    ctrl->dwFrameInterval = y16Frame->dwDefaultFrameInterval;
    res = uvc_probe_stream_ctrl(devh, ctrl);
    if (res < 0) {
        std::snprintf(reason, reasonSize, "probe_stream_ctrl: %s", uvc_strerror(res));
    }
    return res;
}

}  // namespace

namespace Components {

LeptonCamera::Status LeptonCamera::open(char* reason, U32 reasonSize) {
    if (m_streaming) {
        return OK;  // idempotent
    }

    // define libuvc handles
    uvc_context_t* ctx = nullptr;
    uvc_device_t* dev = nullptr;
    uvc_device_handle_t* devh = nullptr;
    uvc_stream_handle_t* strmh = nullptr;
    uvc_stream_ctrl_t* ctrl = new uvc_stream_ctrl_t();
    uvc_error_t res;
    char buf[80];

    // init libuvc so we can use it
    res = uvc_init(&ctx, nullptr);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "uvc_init: %s", uvc_strerror(res));
        goto fail;
    }

    // find the first UVC device since the Lepton is the only camera connected
    res = uvc_find_device(ctx, &dev, 0, 0, nullptr);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "uvc_find_device: %s", uvc_strerror(res));
        goto fail;
    }

    // open the device and start streaming
    res = uvc_open(dev, &devh);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "uvc_open: %s", uvc_strerror(res));
        goto fail;
    }

    // Build the stream control for the Lepton's native Y16 format by enumerating
    // the device's descriptors. uvc_get_stream_ctrl_format_size() can't be used
    // here: this libuvc's GUID table doesn't recognize "Y16 ", so a GRAY16/Y16
    // request matches nothing and returns "Invalid mode" (see probeY16StreamCtrl).
    res = probeY16StreamCtrl(devh, ctrl, WIDTH, HEIGHT, buf, sizeof(buf));
    if (res < 0) {
        goto fail;  // buf already holds the diagnostic
    }

    // open the stream and start the callback thread
    res = uvc_stream_open_ctrl(devh, &strmh, ctrl);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "stream_open: %s", uvc_strerror(res));
        goto fail;
    }
    res = uvc_stream_start(strmh, leptonFrameCallback, this, 0);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "stream_start: %s", uvc_strerror(res));
        goto fail;
    }

    // update libuvc handles to indicate streaming is active
    m_ctx = ctx;
    m_dev = dev;
    m_devh = devh;
    m_strmh = strmh;
    m_ctrl = ctrl;
    m_streaming = true;
    return OK;

fail:
    writeReason(reason, reasonSize, buf);
    if (strmh != nullptr) { // stream open failed
        uvc_stream_close(strmh);
    }
    if (devh != nullptr) { // device open failed
        uvc_close(devh);
    }
    if (dev != nullptr) { // device find failed
        uvc_unref_device(dev);
    }
    if (ctx != nullptr) { // libuvc init failed
        uvc_exit(ctx);
    }
    delete ctrl;
    return LIBUVC_ERROR;
}

void LeptonCamera::close() {
    if (m_strmh != nullptr) {
        uvc_stream_close(static_cast<uvc_stream_handle_t*>(m_strmh));  // stops streaming + frees
        m_strmh = nullptr;
    }
    if (m_devh != nullptr) {
        uvc_close(static_cast<uvc_device_handle_t*>(m_devh));
        m_devh = nullptr;
    }
    if (m_dev != nullptr) {
        uvc_unref_device(static_cast<uvc_device_t*>(m_dev));
        m_dev = nullptr;
    }
    if (m_ctx != nullptr) {
        uvc_exit(static_cast<uvc_context_t*>(m_ctx));
        m_ctx = nullptr;
    }
    if (m_ctrl != nullptr) {
        delete static_cast<uvc_stream_ctrl_t*>(m_ctrl);
        m_ctrl = nullptr;
    }
    m_streaming = false;
    Os::ScopeLock lock(m_mutex);
    m_haveValidFrame = false;
}

}  // namespace Components

// #else // libuvc not available, use dummy values for building and testing without hardware

// namespace Components {

// LeptonCamera::Status LeptonCamera::open(char* reason, U32 reasonSize) {
//     (void)reason;
//     (void)reasonSize;
//     // Deterministic ramp so the data-product path produces identical, verifiable
//     // output without hardware (matches the original CAPTURE_IMAGE stub).
//     Os::ScopeLock lock(m_mutex);
//     for (U32 i = 0; i < NUM_PIXELS; i++) {
//         m_latestFrame[i] = static_cast<U16>(i);
//     }
//     m_haveValidFrame = true;
//     m_streaming = true;
//     return OK;
// }

// void LeptonCamera::close() {
//     m_streaming = false;
//     Os::ScopeLock lock(m_mutex);
//     m_haveValidFrame = false;
// }

// }  // namespace Components

// #endif  // LEPTON_USE_LIBUVC