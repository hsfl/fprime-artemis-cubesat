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
// nearly uniform/blank. Heuristic ported from the Python reference: a frame is
// valid when fewer than 80% of its pixels read below 1000.
bool isValidFrame(const U16* px, U32 numPixels) {
    U32 lowCount = 0;
    for (U32 i = 0; i < numPixels; i++) {
        if (px[i] < 1000) {
            lowCount++;
        }
    }
    return lowCount < ((numPixels * 4) / 5);
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

LeptonCamera::Status LeptonCamera::getLatestFrame(U16* out,
                                                  U32 numPixels,
                                                  U32 timeoutMs,
                                                  char* reason,
                                                  U32 reasonSize) {
    if (!m_streaming) {
        writeReason(reason, reasonSize, "camera not streaming (ENABLE first)");
        return NOT_STREAMING;
    }
    if (out == nullptr || numPixels != NUM_PIXELS) {
        writeReason(reason, reasonSize, "frame buffer size mismatch");
        return DEVICE_ERROR;
    }

    // Poll the latest-frame buffer until a valid frame exists or we time out.
    // The wait covers the FFC settle right after open().
    const U32 pollMs = 10;
    U32 waitedMs = 0;
    for (;;) {
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
#ifdef LEPTON_USE_LIBUVC

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

}  // namespace

namespace Components {

LeptonCamera::Status LeptonCamera::open(char* reason, U32 reasonSize) {
    if (m_streaming) {
        return OK;  // idempotent
    }

    uvc_context_t* ctx = nullptr;
    uvc_device_t* dev = nullptr;
    uvc_device_handle_t* devh = nullptr;
    uvc_stream_handle_t* strmh = nullptr;
    uvc_stream_ctrl_t* ctrl = new uvc_stream_ctrl_t();
    uvc_error_t res;
    char buf[80];

    res = uvc_init(&ctx, nullptr);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "uvc_init: %s", uvc_strerror(res));
        goto fail;
    }
    // vid=0, pid=0, sn=NULL -> first UVC device (the Lepton is the only one).
    res = uvc_find_device(ctx, &dev, 0, 0, nullptr);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "uvc_find_device: %s", uvc_strerror(res));
        goto fail;
    }
    res = uvc_open(dev, &devh);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "uvc_open: %s", uvc_strerror(res));
        goto fail;
    }
    // Lepton 3.x native mode: 16-bit thermal (Y16), 160x120, 9 fps. This libuvc                                  
    // exposes 16-bit grayscale as UVC_FRAME_FORMAT_GRAY16 (== the camera's Y16).
    res = uvc_get_stream_ctrl_format_size(devh, ctrl, UVC_FRAME_FORMAT_GRAY16,
                                          static_cast<int>(WIDTH), static_cast<int>(HEIGHT), 9);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "stream_ctrl: %s", uvc_strerror(res));
        goto fail;
    }
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

    m_ctx = ctx;
    m_dev = dev;
    m_devh = devh;
    m_strmh = strmh;
    m_ctrl = ctrl;
    m_streaming = true;
    return OK;

fail:
    writeReason(reason, reasonSize, buf);
    if (strmh != nullptr) {
        uvc_stream_close(strmh);
    }
    if (devh != nullptr) {
        uvc_close(devh);
    }
    if (dev != nullptr) {
        uvc_unref_device(dev);
    }
    if (ctx != nullptr) {
        uvc_exit(ctx);
    }
    delete ctrl;
    return DEVICE_ERROR;
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

// ======================================================================
// Ramp stub (no libuvc on this host)
// ======================================================================
#else

namespace Components {

LeptonCamera::Status LeptonCamera::open(char* reason, U32 reasonSize) {
    (void)reason;
    (void)reasonSize;
    // Deterministic ramp so the data-product path produces identical, verifiable
    // output without hardware (matches the original CAPTURE_IMAGE stub).
    Os::ScopeLock lock(m_mutex);
    for (U32 i = 0; i < NUM_PIXELS; i++) {
        m_latestFrame[i] = static_cast<U16>(i);
    }
    m_haveValidFrame = true;
    m_streaming = true;
    return OK;
}

void LeptonCamera::close() {
    m_streaming = false;
    Os::ScopeLock lock(m_mutex);
    m_haveValidFrame = false;
}

}  // namespace Components

#endif  // LEPTON_USE_LIBUVC