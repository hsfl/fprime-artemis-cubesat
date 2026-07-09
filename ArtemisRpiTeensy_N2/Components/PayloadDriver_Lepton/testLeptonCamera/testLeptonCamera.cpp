// ======================================================================
// \title  testLeptonCamera.cpp
// \brief  Standalone F Prime-free harness for the LeptonCamera libuvc path.
//
// Build + run on the Pi:
//   g++ -std=c++17 testLeptonCamera.cpp -o testLeptonCamera \
//       -I/usr/local/include -L/usr/local/lib -luvc -lusb-1.0 -lpthread
//   LEPTON_CAMERA_BACKEND=uvc LD_LIBRARY_PATH=/usr/local/lib ./testLeptonCamera
// ======================================================================

#include "testLeptonCamera.hpp"

#include <libuvc/libuvc.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

bool isValidFrame(const U16* pixels, U32 numPixels) {
    U32 invalidCount = 0U;
    for (U32 i = 0; i < numPixels; i++) {
        if (pixels[i] < 1000U) {
            invalidCount++;
        }
    }
    return invalidCount < ((numPixels * 4U) / 5U);
}

void writeReason(char* reason, U32 reasonSize, const char* message) {
    if ((reason != nullptr) && (reasonSize > 0U)) {
        std::snprintf(reason, reasonSize, "%s", message);
    }
}

void leptonFrameCallback(uvc_frame_t* frame, void* userPtr) {
    if ((frame == nullptr) || (userPtr == nullptr) || (frame->data == nullptr)) {
        return;
    }
    Components::LeptonCamera* self = static_cast<Components::LeptonCamera*>(userPtr);
    self->ingestFrameRaw(frame->data, static_cast<U32>(frame->width * frame->height));
}

void describeModes(uvc_device_handle_t* devh, char* out, U32 outSize) {
    out[0] = '\0';
    int off = 0;
    for (const uvc_format_desc_t* fmt = uvc_get_format_descs(devh);
         (fmt != nullptr) && (off < static_cast<int>(outSize) - 1);
         fmt = fmt->next) {
        for (const uvc_frame_desc_t* frame = fmt->frame_descs;
             (frame != nullptr) && (off < static_cast<int>(outSize) - 1);
             frame = frame->next) {
            const int n = std::snprintf(out + off,
                                        outSize - static_cast<U32>(off),
                                        "%.4s%ux%u ",
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

uvc_error_t probeY16StreamCtrl(uvc_device_handle_t* devh,
                               uvc_stream_ctrl_t* ctrl,
                               U32 width,
                               U32 height,
                               char* reason,
                               U32 reasonSize) {
    const uvc_format_desc_t* y16Fmt = nullptr;
    const uvc_frame_desc_t* y16Frame = nullptr;
    for (const uvc_format_desc_t* fmt = uvc_get_format_descs(devh); (fmt != nullptr) && (y16Frame == nullptr);
         fmt = fmt->next) {
        if (std::memcmp(fmt->fourccFormat, "Y16 ", 4) != 0) {
            continue;
        }
        for (const uvc_frame_desc_t* frame = fmt->frame_descs; frame != nullptr; frame = frame->next) {
            if ((frame->wWidth == width) && (frame->wHeight == height)) {
                y16Fmt = fmt;
                y16Frame = frame;
                break;
            }
        }
    }
    if (y16Frame == nullptr) {
        char modes[96];
        describeModes(devh, modes, sizeof(modes));
        std::snprintf(reason,
                      reasonSize,
                      "no Y16 %ux%u; have: %s",
                      static_cast<unsigned>(width),
                      static_cast<unsigned>(height),
                      modes);
        return UVC_ERROR_INVALID_MODE;
    }

    const unsigned fps = (y16Frame->dwDefaultFrameInterval != 0)
                             ? static_cast<unsigned>(10000000UL / y16Frame->dwDefaultFrameInterval)
                             : 9U;
    const uvc_frame_format knownFormats[] = {UVC_FRAME_FORMAT_UYVY, UVC_FRAME_FORMAT_GRAY8, UVC_FRAME_FORMAT_YUYV};
    uvc_error_t res = UVC_ERROR_INVALID_MODE;
    for (const uvc_frame_format format : knownFormats) {
        res = uvc_get_stream_ctrl_format_size(
            devh, ctrl, format, static_cast<int>(width), static_cast<int>(height), static_cast<int>(fps));
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

LeptonCamera::LeptonCamera() : m_streaming(false), m_mutex(), m_haveValidFrame(false), m_latestFrame{} {
    this->m_ctx = nullptr;
    this->m_dev = nullptr;
    this->m_devh = nullptr;
    this->m_strmh = nullptr;
    this->m_ctrl = nullptr;
}

LeptonCamera::~LeptonCamera() {
    this->close();
}

bool LeptonCamera::isStreaming() const {
    return this->m_streaming;
}

void LeptonCamera::ingestFrameRaw(const void* data, U32 numPixels) {
    if ((data == nullptr) || (numPixels != NUM_PIXELS)) {
        return;
    }
    const U16* pixels = static_cast<const U16*>(data);
    if (!isValidFrame(pixels, numPixels)) {
        return;
    }
    std::lock_guard<std::mutex> lock(this->m_mutex);
    std::memcpy(this->m_latestFrame, pixels, numPixels * sizeof(U16));
    this->m_haveValidFrame = true;
}

LeptonCamera::Status LeptonCamera::getLatestFrame(U16* out,
                                                  U32 numPixels,
                                                  U32 timeoutMs,
                                                  char* reason,
                                                  U32 reasonSize) {
    if (!this->m_streaming) {
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

    constexpr U32 POLL_MS = 10U;
    U32 waitedMs = 0U;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(this->m_mutex);
            if (this->m_haveValidFrame) {
                std::memcpy(out, this->m_latestFrame, numPixels * sizeof(U16));
                return OK;
            }
        }
        if (waitedMs >= timeoutMs) {
            writeReason(reason, reasonSize, "timed out waiting for a valid frame");
            return FRAME_TIMEOUT;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
        waitedMs += POLL_MS;
    }
}

LeptonCamera::Status LeptonCamera::open(char* reason, U32 reasonSize) {
    if (this->m_streaming) {
        return OK;
    }

    uvc_context_t* ctx = nullptr;
    uvc_device_t* dev = nullptr;
    uvc_device_handle_t* devh = nullptr;
    uvc_stream_handle_t* strmh = nullptr;
    uvc_stream_ctrl_t* ctrl = new uvc_stream_ctrl_t();
    uvc_error_t res = UVC_SUCCESS;
    char buf[96] = {};

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

    this->m_ctx = ctx;
    this->m_dev = dev;
    this->m_devh = devh;
    this->m_strmh = strmh;
    this->m_ctrl = ctrl;
    this->m_streaming = true;
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
    if (this->m_strmh != nullptr) {
        uvc_stream_close(static_cast<uvc_stream_handle_t*>(this->m_strmh));
        this->m_strmh = nullptr;
    }
    if (this->m_devh != nullptr) {
        uvc_close(static_cast<uvc_device_handle_t*>(this->m_devh));
        this->m_devh = nullptr;
    }
    if (this->m_dev != nullptr) {
        uvc_unref_device(static_cast<uvc_device_t*>(this->m_dev));
        this->m_dev = nullptr;
    }
    if (this->m_ctx != nullptr) {
        uvc_exit(static_cast<uvc_context_t*>(this->m_ctx));
        this->m_ctx = nullptr;
    }
    if (this->m_ctrl != nullptr) {
        delete static_cast<uvc_stream_ctrl_t*>(this->m_ctrl);
        this->m_ctrl = nullptr;
    }
    this->m_streaming = false;
    std::lock_guard<std::mutex> lock(this->m_mutex);
    this->m_haveValidFrame = false;
}

}  // namespace Components

int main() {
    const char* backend = std::getenv("LEPTON_CAMERA_BACKEND");
    if ((backend != nullptr) && (backend[0] != '\0') && (std::strcmp(backend, "uvc") != 0)) {
        std::printf("LEPTON_CAMERA_BACKEND must be uvc for this HIL harness, got '%s'\n", backend);
        return 3;
    }

    Components::LeptonCamera camera;
    char reason[96] = {};

    std::printf("open()...\n");
    if (camera.open(reason, sizeof(reason)) != Components::LeptonCamera::OK) {
        std::printf("  open FAILED: %s\n", reason);
        return 1;
    }
    std::printf("  open OK, streaming\n");

    static U16 frame[Components::LeptonCamera::NUM_PIXELS];
    std::printf("getLatestFrame() (5s timeout)...\n");
    const Components::LeptonCamera::Status status =
        camera.getLatestFrame(frame, Components::LeptonCamera::NUM_PIXELS, 5000U, reason, sizeof(reason));
    if (status != Components::LeptonCamera::OK) {
        std::printf("  getLatestFrame FAILED (status=%d): %s\n", static_cast<int>(status), reason);
        camera.close();
        return 2;
    }

    U16 minValue = 0xFFFFU;
    U16 maxValue = 0U;
    unsigned long sum = 0UL;
    for (U32 i = 0; i < Components::LeptonCamera::NUM_PIXELS; i++) {
        if (frame[i] < minValue) {
            minValue = frame[i];
        }
        if (frame[i] > maxValue) {
            maxValue = frame[i];
        }
        sum += frame[i];
    }
    std::printf("  frame OK: min=%u max=%u mean=%lu sample=%u,%u,%u,%u\n",
                minValue,
                maxValue,
                sum / Components::LeptonCamera::NUM_PIXELS,
                frame[0],
                frame[1],
                frame[2],
                frame[3]);

    camera.close();
    std::printf("DONE\n");
    return 0;
}
