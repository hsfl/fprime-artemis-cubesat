// ======================================================================
// \title  testLeptonCamera.cpp
// \author samanthamallari
// \brief  Standalone (F'-free) harness for the LeptonCamera libuvc path.
//
// Verifies the RPi can open the Lepton, negotiate the Y16 stream, and deliver
// valid frames -- the whole chain LeptonCamera::open()/getLatestFrame() exercise
// -- WITHOUT the F' framework, so it builds + runs natively on the Pi in seconds
// instead of one cross-compile/deploy cycle per surprise.
//
// Build + run on the Pi:
//   g++ -std=c++17 testLeptonCamera.cpp -o testLeptonCamera \
//       -I/usr/local/include -L/usr/local/lib -luvc -lusb-1.0 -lpthread
//   LD_LIBRARY_PATH=/usr/local/lib ./testLeptonCamera
//
// Logic is a 1:1 port of Components/PayloadAdapter_Lepton/LeptonCamera.cpp; the
// only changes are Os::Mutex -> std::mutex and Os::Task::delay -> std::thread
// sleep. Fixes proven here copy straight back.
// ======================================================================

#include "testLeptonCamera.hpp"

#include <libuvc/libuvc.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

// FFC (Flat-Field Correction) frames right after open() are nearly blank. A
// frame is valid when fewer than 80% of its pixels read below 1000.
bool isValidFrame(const U16* px, U32 numPixels) {
    U32 inValidCount = 0;
    for (U32 i = 0; i < numPixels; i++) {
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

// libuvc streaming-thread callback: hand each frame to the owning LeptonCamera.
void leptonFrameCallback(uvc_frame_t* frame, void* userPtr) {
    if (frame == nullptr || userPtr == nullptr || frame->data == nullptr) {
        return;
    }
    Components::LeptonCamera* self = static_cast<Components::LeptonCamera*>(userPtr);
    self->ingestFrameRaw(frame->data, static_cast<U32>(frame->width * frame->height));
}

// Enumerate the device's modes (fourcc + WxH) into out, truncated to the buffer.
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

// Build a stream control for the Lepton's native Y16 format. See LeptonCamera.cpp
// for the full rationale: this libuvc's GUID table doesn't recognize "Y16 ", so
// uvc_get_stream_ctrl_format_size() can't match it directly. We find Y16 by raw
// fourcc, bootstrap a valid control (esp. bInterfaceNumber) via a sibling format
// libuvc DOES recognize, then repoint it at Y16 and renegotiate.
uvc_error_t probeY16StreamCtrl(uvc_device_handle_t* devh,
                               uvc_stream_ctrl_t* ctrl,
                               U32 width,
                               U32 height,
                               char* reason,
                               U32 reasonSize) {
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

LeptonCamera::LeptonCamera() : m_streaming(false), m_haveValidFrame(false) {
    std::memset(m_latestFrame, 0, sizeof(m_latestFrame));
    m_ctx = nullptr;
    m_dev = nullptr;
    m_devh = nullptr;
    m_strmh = nullptr;
    m_ctrl = nullptr;
}

LeptonCamera::~LeptonCamera() {
    this->close();
}

bool LeptonCamera::isStreaming() const {
    return m_streaming;
}

void LeptonCamera::ingestFrameRaw(const void* data, U32 numPixels) {
    if (data == nullptr || numPixels != NUM_PIXELS) {
        return;
    }
    const U16* px = static_cast<const U16*>(data);
    if (!isValidFrame(px, numPixels)) {
        return;  // drop FFC / blank frames; keep the last good one
    }
    std::lock_guard<std::mutex> lock(m_mutex);
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

    const U32 pollMs = 10;
    U32 waitedMs = 0;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_haveValidFrame) {
                std::memcpy(out, m_latestFrame, numPixels * sizeof(U16));
                return OK;
            }
        }
        if (waitedMs >= timeoutMs) {
            writeReason(reason, reasonSize, "timed out waiting for a valid frame");
            return FRAME_TIMEOUT;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        waitedMs += pollMs;
    }
}

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
    char buf[96];

    res = uvc_init(&ctx, nullptr);
    if (res < 0) {
        std::snprintf(buf, sizeof(buf), "uvc_init: %s", uvc_strerror(res));
        goto fail;
    }

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

    res = probeY16StreamCtrl(devh, ctrl, WIDTH, HEIGHT, buf, sizeof(buf));
    if (res < 0) {
        goto fail;  // buf already holds the diagnostic
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
    return LIBUVC_ERROR;
}

void LeptonCamera::close() {
    if (m_strmh != nullptr) {
        uvc_stream_close(static_cast<uvc_stream_handle_t*>(m_strmh));
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
    std::lock_guard<std::mutex> lock(m_mutex);
    m_haveValidFrame = false;
}

}  // namespace Components

// ----------------------------------------------------------------------
// Test driver
// ----------------------------------------------------------------------

int main() {
    Components::LeptonCamera cam;
    char reason[96] = {0};

    std::printf("open()...\n");
    if (cam.open(reason, sizeof(reason)) != Components::LeptonCamera::OK) {
        std::printf("  open FAILED: %s\n", reason);
        return 1;
    }
    std::printf("  open OK, streaming\n");

    // First valid frame can take a few FFC cycles after open(); allow 5s.
    static U16 frame[Components::LeptonCamera::NUM_PIXELS];
    std::printf("getLatestFrame() (5s timeout)...\n");
    const Components::LeptonCamera::Status st =
        cam.getLatestFrame(frame, Components::LeptonCamera::NUM_PIXELS, 5000, reason, sizeof(reason));

    if (st != Components::LeptonCamera::OK) {
        std::printf("  getLatestFrame FAILED (status=%d): %s\n", static_cast<int>(st), reason);
        cam.close();
        return 2;
    }

    // Summarize the frame so a "success" is actually meaningful (not all zeros).
    U16 mn = 0xFFFF, mx = 0;
    unsigned long sum = 0;
    for (U32 i = 0; i < Components::LeptonCamera::NUM_PIXELS; i++) {
        if (frame[i] < mn) mn = frame[i];
        if (frame[i] > mx) mx = frame[i];
        sum += frame[i];
    }
    std::printf("  frame OK: min=%u max=%u mean=%lu  (sample: %u %u %u %u)\n",
                mn, mx, sum / Components::LeptonCamera::NUM_PIXELS, frame[0], frame[1], frame[2], frame[3]);

    cam.close();
    std::printf("DONE\n");
    return 0;
}
