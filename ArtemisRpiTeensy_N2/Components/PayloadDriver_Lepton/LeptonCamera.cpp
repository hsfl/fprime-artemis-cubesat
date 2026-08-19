// ======================================================================
// \title  LeptonCamera.cpp
// \brief  libuvc wrapper for the FLIR Lepton plus sample/synthetic fallback.
//
// The libuvc implementation is ported from
// EPSCOR_C3M_REFACTOR:PayloadAdapter_Lepton/LeptonCamera.cpp. The old branch
// had the real implementation in place but its #ifdef split was commented out;
// this file keeps the full real backend and restores the compile-time split.
// ======================================================================

#include "Components/PayloadDriver_Lepton/LeptonCamera.hpp"

#include <Fw/Time/TimeInterval.hpp>
#include <Os/Task.hpp>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef LEPTON_USE_LIBUVC
#include <libuvc/libuvc.h>
#endif

namespace {

constexpr const char* BACKEND_ENV = "LEPTON_CAMERA_BACKEND";
constexpr const char* SAMPLE_ENV = "C3M_LEPTON_SAMPLE_CSV";
// Lepton temperatures are reported in centikelvin, so values below 1000 are
// physically impossible here. Tolerate a small number of bad sensor pixels,
// but never accept the large zero-filled tail produced by a partial UVC frame.
constexpr U32 MAX_INVALID_PIXELS = Components::LeptonCamera::NUM_PIXELS / 100U;

bool stringsEqual(const char* lhs, const char* rhs) {
    return (lhs != nullptr) && (rhs != nullptr) && (std::strcmp(lhs, rhs) == 0);
}

}  // namespace

namespace Components {

LeptonCamera::LeptonCamera()
    : m_streaming(false),
      m_activeBackend(BACKEND_AUTO),
      m_frameCounter(0),
      m_mutex(),
      m_haveValidFrame(false),
      m_latestFrame{} {
#ifdef LEPTON_USE_LIBUVC
    this->m_ctx = nullptr;
    this->m_dev = nullptr;
    this->m_devh = nullptr;
    this->m_strmh = nullptr;
    this->m_ctrl = nullptr;
#endif
}

LeptonCamera::~LeptonCamera() {
    this->close();
}

LeptonCamera::Status LeptonCamera::open(char* reason, U32 reasonSize) {
    if (this->m_streaming) {
        writeReason(reason, reasonSize, this->backendName());
        return OK;
    }

    bool validBackend = false;
    const Backend requested = parseBackend(std::getenv(BACKEND_ENV), validBackend);
    if (!validBackend) {
        writeReason(reason, reasonSize, "invalid LEPTON_CAMERA_BACKEND (use uvc|sample|synthetic|auto)");
        return LIBUVC_ERROR;
    }

    {
        Os::ScopeLock lock(this->m_mutex);
        this->m_haveValidFrame = false;
    }

    switch (requested) {
        case BACKEND_UVC:
            return this->openUvc(reason, reasonSize);
        case BACKEND_SAMPLE:
            return this->openSample(reason, reasonSize);
        case BACKEND_SYNTHETIC:
            return this->openSynthetic(reason, reasonSize);
        case BACKEND_AUTO:
        default:
            return this->openAuto(reason, reasonSize);
    }
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

    if (this->m_activeBackend == BACKEND_SYNTHETIC) {
        this->fillSyntheticFrame();
    }

    constexpr U32 POLL_MS = 10U;
    U32 waitedMs = 0U;
    for (;;) {
        {
            Os::ScopeLock lock(this->m_mutex);
            if (this->m_haveValidFrame) {
                std::memcpy(out, this->m_latestFrame, numPixels * sizeof(U16));
                return OK;
            }
        }
        if (waitedMs >= timeoutMs) {
            writeReason(reason, reasonSize, "timed out waiting for a valid frame");
            return FRAME_TIMEOUT;
        }
        (void)Os::Task::delay(Fw::TimeInterval(0, POLL_MS * 1000U));
        waitedMs += POLL_MS;
    }
}

void LeptonCamera::close() {
#ifdef LEPTON_USE_LIBUVC
    this->m_streaming = false;
    if (this->m_strmh != nullptr) {
        uvc_stream_handle_t* strmh = static_cast<uvc_stream_handle_t*>(this->m_strmh);
        (void)uvc_stream_stop(strmh);
        uvc_stream_close(strmh);
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
#endif
    this->m_streaming = false;
    Os::ScopeLock lock(this->m_mutex);
    this->m_haveValidFrame = false;
}

bool LeptonCamera::isStreaming() const {
    return this->m_streaming;
}

const char* LeptonCamera::backendName() const {
    return backendName(this->m_activeBackend);
}

void LeptonCamera::ingestFrameRaw(const void* data, U32 numPixels) {
    if ((data == nullptr) || (numPixels != NUM_PIXELS)) {
        return;
    }
    const U16* pixels = static_cast<const U16*>(data);
    if (!isValidFrame(pixels, numPixels)) {
        return;
    }

    Os::ScopeLock lock(this->m_mutex);
    std::memcpy(this->m_latestFrame, pixels, numPixels * sizeof(U16));
    this->m_haveValidFrame = true;
}

LeptonCamera::Backend LeptonCamera::parseBackend(const char* value, bool& valid) {
    valid = true;
    if ((value == nullptr) || (value[0] == '\0') || stringsEqual(value, "auto")) {
        return BACKEND_AUTO;
    }
    if (stringsEqual(value, "uvc")) {
        return BACKEND_UVC;
    }
    if (stringsEqual(value, "sample")) {
        return BACKEND_SAMPLE;
    }
    if (stringsEqual(value, "synthetic")) {
        return BACKEND_SYNTHETIC;
    }

    valid = false;
    return BACKEND_AUTO;
}

const char* LeptonCamera::backendName(Backend backend) {
    switch (backend) {
        case BACKEND_UVC:
            return "uvc";
        case BACKEND_SAMPLE:
            return "sample";
        case BACKEND_SYNTHETIC:
            return "synthetic";
        case BACKEND_AUTO:
        default:
            return "auto";
    }
}

void LeptonCamera::writeReason(char* reason, U32 reasonSize, const char* message) {
    if ((reason != nullptr) && (reasonSize > 0U)) {
        std::snprintf(reason, reasonSize, "%s", message);
    }
}

bool LeptonCamera::isValidFrame(const U16* pixels, U32 numPixels) {
    if ((pixels == nullptr) || (numPixels == 0U)) {
        return false;
    }

    U32 invalidCount = 0U;
    for (U32 i = 0; i < numPixels; i++) {
        if (pixels[i] < 1000U) {
            invalidCount++;
        }
    }
    return invalidCount <= MAX_INVALID_PIXELS;
}

LeptonCamera::Status LeptonCamera::openAuto(char* reason, U32 reasonSize) {
#ifdef LEPTON_USE_LIBUVC
    char uvcReason[96] = {};
    if (this->openUvc(uvcReason, sizeof(uvcReason)) == OK) {
        writeReason(reason, reasonSize, "uvc");
        return OK;
    }
#endif
    if (this->loadSampleFrame(reason, reasonSize)) {
        this->m_activeBackend = BACKEND_SAMPLE;
        this->m_streaming = true;
        writeReason(reason, reasonSize, "sample");
        return OK;
    }
    return this->openSynthetic(reason, reasonSize);
}

LeptonCamera::Status LeptonCamera::openSample(char* reason, U32 reasonSize) {
    const char* samplePath = std::getenv(SAMPLE_ENV);
    if ((samplePath == nullptr) || (samplePath[0] == '\0')) {
        writeReason(reason, reasonSize, "C3M_LEPTON_SAMPLE_CSV is required for sample backend");
        return LIBUVC_ERROR;
    }
    if (!this->loadSampleCsv(samplePath)) {
        writeReason(reason, reasonSize, "failed to load C3M_LEPTON_SAMPLE_CSV");
        return LIBUVC_ERROR;
    }

    this->m_activeBackend = BACKEND_SAMPLE;
    this->m_streaming = true;
    writeReason(reason, reasonSize, "sample");
    return OK;
}

LeptonCamera::Status LeptonCamera::openSynthetic(char* reason, U32 reasonSize) {
    this->m_activeBackend = BACKEND_SYNTHETIC;
    this->fillSyntheticFrame();
    this->m_streaming = true;
    writeReason(reason, reasonSize, "synthetic");
    return OK;
}

bool LeptonCamera::loadSampleFrame(char* reason, U32 reasonSize) {
    const char* envPath = std::getenv(SAMPLE_ENV);
    if ((envPath != nullptr) && (envPath[0] != '\0') && this->loadSampleCsv(envPath)) {
        writeReason(reason, reasonSize, "sample");
        return true;
    }

    const char* candidates[] = {
        "../ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv",
        "ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv",
        "../../ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv",
    };
    for (const char* candidate : candidates) {
        if (this->loadSampleCsv(candidate)) {
            writeReason(reason, reasonSize, "sample");
            return true;
        }
    }
    return false;
}

bool LeptonCamera::loadSampleCsv(const char* path) {
    if ((path == nullptr) || (path[0] == '\0')) {
        return false;
    }

    std::FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return false;
    }

    U16 frame[NUM_PIXELS] = {};
    U32 count = 0U;
    char line[2048] = {};
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        if ((line[0] == '#') || (line[0] == '\n') || (line[0] == '\r')) {
            continue;
        }

        char* cursor = line;
        while (*cursor != '\0') {
            while ((*cursor == ' ') || (*cursor == '\t') || (*cursor == ',')) {
                ++cursor;
            }
            if ((*cursor == '\0') || (*cursor == '\n') || (*cursor == '\r')) {
                break;
            }

            errno = 0;
            char* end = nullptr;
            const double celsius = std::strtod(cursor, &end);
            if ((end == cursor) || (errno != 0) || (count >= NUM_PIXELS)) {
                std::fclose(file);
                return false;
            }

            const double centikelvin = (celsius + 273.15) * 100.0;
            if ((centikelvin < 0.0) || (centikelvin > 65535.0)) {
                std::fclose(file);
                return false;
            }
            frame[count] = static_cast<U16>(std::lround(centikelvin));
            count += 1U;
            cursor = end;
        }
    }

    std::fclose(file);
    if ((count != NUM_PIXELS) || !isValidFrame(frame, NUM_PIXELS)) {
        return false;
    }

    Os::ScopeLock lock(this->m_mutex);
    std::memcpy(this->m_latestFrame, frame, sizeof(this->m_latestFrame));
    this->m_haveValidFrame = true;
    return true;
}

void LeptonCamera::fillSyntheticFrame() {
    this->m_frameCounter += 1U;
    constexpr U16 AMBIENT_CK = 29415U;
    constexpr U16 HOTSPOT_CK = 850U;
    U16 frame[NUM_PIXELS] = {};

    for (U32 y = 0; y < HEIGHT; ++y) {
        for (U32 x = 0; x < WIDTH; ++x) {
            const U32 index = (y * WIDTH) + x;
            const U16 gradient = static_cast<U16>(((x * 3U) + (y * 5U) + (this->m_frameCounter * 17U)) % 900U);
            U16 value = static_cast<U16>(AMBIENT_CK + gradient);

            const I32 dx = static_cast<I32>(x) - 96;
            const I32 dy = static_cast<I32>(y) - 58;
            if (((dx * dx) + (dy * dy)) < 420) {
                value = static_cast<U16>(value + HOTSPOT_CK);
            }
            frame[index] = value;
        }
    }

    Os::ScopeLock lock(this->m_mutex);
    std::memcpy(this->m_latestFrame, frame, sizeof(this->m_latestFrame));
    this->m_haveValidFrame = true;
}

}  // namespace Components

#ifdef LEPTON_USE_LIBUVC

namespace {

void leptonFrameCallback(uvc_frame_t* frame, void* userPtr) {
    if ((frame == nullptr) || (userPtr == nullptr) || (frame->data == nullptr)) {
        return;
    }
    constexpr std::size_t EXPECTED_FRAME_BYTES =
        Components::LeptonCamera::NUM_PIXELS * sizeof(U16);
    if ((frame->width != Components::LeptonCamera::WIDTH) ||
        (frame->height != Components::LeptonCamera::HEIGHT) ||
        (frame->data_bytes < EXPECTED_FRAME_BYTES)) {
        return;
    }
    Components::LeptonCamera* self = static_cast<Components::LeptonCamera*>(userPtr);
    self->ingestFrameRaw(frame->data, Components::LeptonCamera::NUM_PIXELS);
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

LeptonCamera::Status LeptonCamera::openUvc(char* reason, U32 reasonSize) {
    if (this->m_streaming) {
        return OK;
    }

    uvc_context_t* ctx = nullptr;
    uvc_device_t* dev = nullptr;
    uvc_device_handle_t* devh = nullptr;
    uvc_stream_handle_t* strmh = nullptr;
    uvc_stream_ctrl_t* ctrl = new uvc_stream_ctrl_t();
    uvc_error_t res = UVC_SUCCESS;
    bool streamStarted = false;
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
    streamStarted = true;

    this->m_ctx = ctx;
    this->m_dev = dev;
    this->m_devh = devh;
    this->m_strmh = strmh;
    this->m_ctrl = ctrl;
    this->m_activeBackend = BACKEND_UVC;
    this->m_streaming = true;
    writeReason(reason, reasonSize, "uvc");
    return OK;

fail:
    writeReason(reason, reasonSize, buf);
    if (strmh != nullptr) {
        if (streamStarted) {
            (void)uvc_stream_stop(strmh);
        }
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

}  // namespace Components

#else

namespace Components {

LeptonCamera::Status LeptonCamera::openUvc(char* reason, U32 reasonSize) {
    writeReason(reason, reasonSize, "LEPTON_CAMERA_BACKEND=uvc requested but libuvc is not compiled in");
    return LIBUVC_ERROR;
}

}  // namespace Components

#endif  // LEPTON_USE_LIBUVC
