// ======================================================================
// \title  BosonCamera.cpp
// \brief  Direct Linux V4L2 plus sample/synthetic Boson capture helper.
// ======================================================================

#include "BosonCamera.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <vector>

#ifdef BOSON_USE_V4L2
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

constexpr const char* BACKEND_ENV = "BOSON_CAMERA_BACKEND";
#ifdef BOSON_USE_V4L2
constexpr const char* DEVICE_ENV = "BOSON_V4L2_DEVICE";
#endif
constexpr const char* SAMPLE_ENV = "BOSON_SAMPLE_FILE";
constexpr const char* SAMPLE_PATH_ENV = "BOSON_SAMPLE_PATH";
constexpr const char* SAMPLE_RAW_ENV = "BOSON_SAMPLE_RAW";
constexpr U32 BYTES_PER_PIXEL = 2U;
constexpr U32 ROW_BYTES = Components::BosonCamera::WIDTH * BYTES_PER_PIXEL;
constexpr std::size_t FRAME_BYTES_256 =
    static_cast<std::size_t>(Components::BosonCamera::WIDTH) *
    Components::BosonCamera::HEIGHT * BYTES_PER_PIXEL;
constexpr std::size_t FRAME_BYTES_258 =
    static_cast<std::size_t>(Components::BosonCamera::WIDTH) *
    (Components::BosonCamera::HEIGHT + Components::BosonCamera::TELEMETRY_ROWS) * BYTES_PER_PIXEL;
constexpr U16 STARTUP_PLACEHOLDER = 0x8080U;
constexpr U32 PLACEHOLDER_REJECT_PERCENT = 99U;

bool stringsEqual(const char* lhs, const char* rhs) {
    return (lhs != nullptr) && (rhs != nullptr) && (std::strcmp(lhs, rhs) == 0);
}

U16 readU16Le(const unsigned char* data) {
    return static_cast<U16>(static_cast<U16>(data[0]) |
                            (static_cast<U16>(data[1]) << 8U));
}

const char* samplePathFromEnvironment() {
    const char* path = std::getenv(SAMPLE_ENV);
    if ((path != nullptr) && (path[0] != '\0')) {
        return path;
    }
    path = std::getenv(SAMPLE_PATH_ENV);
    if ((path != nullptr) && (path[0] != '\0')) {
        return path;
    }
    return std::getenv(SAMPLE_RAW_ENV);
}

#ifdef BOSON_USE_V4L2

struct MmapBuffer {
    void* start;
    std::size_t length;
};

int ioctlRetry(int fd, unsigned long request, void* argument) {
    int result = -1;
    do {
        result = ::ioctl(fd, request, argument);
    } while ((result < 0) && (errno == EINTR));
    return result;
}

bool queueV4l2Buffer(int fd, U32 index) {
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    return ioctlRetry(fd, VIDIOC_QBUF, &buffer) >= 0;
}

#endif

}  // namespace

namespace Components {

BosonCamera::BosonCamera()
    : m_streaming(false),
      m_activeBackend(defaultBackend()),
      m_latestFrame{}
#ifdef __linux__
      ,
      m_v4l2Fd(-1),
      m_v4l2Buffers(nullptr),
      m_v4l2BufferCount(0U),
      m_v4l2Stride(0U),
      m_v4l2Height(0U),
      m_v4l2StreamOn(false)
#endif
{
}

BosonCamera::~BosonCamera() {
    this->close();
}

BosonCamera::Status BosonCamera::open(char* reason, U32 reasonSize) {
    if (this->m_streaming) {
        writeReason(reason, reasonSize, this->backendName());
        return OK;
    }

    bool validBackend = false;
    const Backend requested = parseBackend(std::getenv(BACKEND_ENV), validBackend);
    if (!validBackend) {
        writeReason(reason, reasonSize,
                    "invalid BOSON_CAMERA_BACKEND (use v4l2|sample|synthetic)");
        return INVALID_BACKEND;
    }

    this->m_activeBackend = requested;
    switch (requested) {
        case BACKEND_V4L2:
            return this->openV4l2(reason, reasonSize);
        case BACKEND_SAMPLE:
            return this->openSample(reason, reasonSize);
        case BACKEND_SYNTHETIC:
            return this->openSynthetic(reason, reasonSize);
        default:
            writeReason(reason, reasonSize, "invalid Boson backend");
            return INVALID_BACKEND;
    }
}

BosonCamera::Status BosonCamera::getLatestFrame(U16* out,
                                                U32 numPixels,
                                                U32 timeoutMs,
                                                char* reason,
                                                U32 reasonSize) {
    if (!this->m_streaming) {
        writeReason(reason, reasonSize, "camera not streaming");
        return STREAM_NOT_READY;
    }
    if (out == nullptr) {
        writeReason(reason, reasonSize, "null output buffer");
        return INVALID_ARGUMENT;
    }
    if (numPixels != NUM_PIXELS) {
        writeReason(reason, reasonSize, "frame buffer size mismatch");
        return INVALID_ARGUMENT;
    }

    if (this->m_activeBackend == BACKEND_V4L2) {
#ifdef BOSON_USE_V4L2
        return this->captureV4l2Frame(out, timeoutMs, reason, reasonSize);
#else
        (void)timeoutMs;
        writeReason(reason, reasonSize, "v4l2 support is unavailable in this build");
        return DEVICE_ERROR;
#endif
    }

    std::memcpy(out, this->m_latestFrame, sizeof(this->m_latestFrame));
    writeReason(reason, reasonSize, this->backendName());
    return OK;
}

void BosonCamera::close() {
#ifdef BOSON_USE_V4L2
    this->closeV4l2();
#endif
    this->m_streaming = false;
}

bool BosonCamera::isStreaming() const {
    return this->m_streaming;
}

const char* BosonCamera::backendName() const {
    return backendName(this->m_activeBackend);
}

bool BosonCamera::isUsableFrame(const U16* pixels, U32 numPixels) {
    if ((pixels == nullptr) || (numPixels == 0U)) {
        return false;
    }

    U32 placeholderCount = 0U;
    for (U32 index = 0U; index < numPixels; index++) {
        if (pixels[index] == STARTUP_PLACEHOLDER) {
            placeholderCount++;
        }
    }
    return (static_cast<U64>(placeholderCount) * 100U) <
           (static_cast<U64>(numPixels) * PLACEHOLDER_REJECT_PERCENT);
}

BosonCamera::Backend BosonCamera::defaultBackend() {
#ifdef __linux__
    // Linux must not silently produce fake flight data.  V4L2 still requires
    // BOSON_V4L2_DEVICE, so an unconfigured process fails closed.
    return BACKEND_V4L2;
#else
    return BACKEND_SYNTHETIC;
#endif
}

BosonCamera::Backend BosonCamera::parseBackend(const char* value, bool& valid) {
    valid = true;
    if ((value == nullptr) || (value[0] == '\0')) {
        return defaultBackend();
    }
    if (stringsEqual(value, "v4l2")) {
        return BACKEND_V4L2;
    }
    if (stringsEqual(value, "sample")) {
        return BACKEND_SAMPLE;
    }
    if (stringsEqual(value, "synthetic")) {
        return BACKEND_SYNTHETIC;
    }

    valid = false;
    return defaultBackend();
}

const char* BosonCamera::backendName(Backend backend) {
    switch (backend) {
        case BACKEND_V4L2:
            return "v4l2";
        case BACKEND_SAMPLE:
            return "sample";
        case BACKEND_SYNTHETIC:
            return "synthetic";
        default:
            return "unknown";
    }
}

void BosonCamera::writeReason(char* reason, U32 reasonSize, const char* message) {
    if ((reason != nullptr) && (reasonSize > 0U)) {
        std::snprintf(reason, reasonSize, "%s", (message != nullptr) ? message : "");
    }
}

BosonCamera::Status BosonCamera::openSample(char* reason, U32 reasonSize) {
    const char* path = samplePathFromEnvironment();
    if ((path == nullptr) || (path[0] == '\0')) {
        writeReason(reason, reasonSize,
                    "BOSON_SAMPLE_FILE is required for sample backend");
        return SAMPLE_ERROR;
    }
    if (!this->loadSampleFrame(path)) {
        writeReason(reason, reasonSize,
                    "sample must be exact raw U16LE 320x256 or 320x258 data");
        return SAMPLE_ERROR;
    }

    this->m_streaming = true;
    writeReason(reason, reasonSize, "sample");
    return OK;
}

BosonCamera::Status BosonCamera::openSynthetic(char* reason, U32 reasonSize) {
    this->fillSyntheticFrame();
    this->m_streaming = true;
    writeReason(reason, reasonSize, "synthetic");
    return OK;
}

bool BosonCamera::loadSampleFrame(const char* path) {
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    const std::streamoff size = file.tellg();
    if ((size != static_cast<std::streamoff>(FRAME_BYTES_256)) &&
        (size != static_cast<std::streamoff>(FRAME_BYTES_258))) {
        return false;
    }
    file.seekg(0, std::ios::beg);

    const std::size_t sourceBytes = static_cast<std::size_t>(size);
    std::vector<unsigned char> raw(sourceBytes);
    file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(sourceBytes));
    if (!file || file.gcount() != static_cast<std::streamsize>(sourceBytes)) {
        return false;
    }

    const U32 sourceRows = (size == static_cast<std::streamoff>(FRAME_BYTES_258))
                               ? (HEIGHT + TELEMETRY_ROWS)
                               : HEIGHT;
    const U32 firstImageRow = (sourceRows == HEIGHT + TELEMETRY_ROWS) ? TELEMETRY_ROWS : 0U;
    for (U32 y = 0U; y < HEIGHT; y++) {
        const U32 sourceRow = y + firstImageRow;
        const std::size_t rowOffset = static_cast<std::size_t>(sourceRow) * ROW_BYTES;
        for (U32 x = 0U; x < WIDTH; x++) {
            this->m_latestFrame[(y * WIDTH) + x] =
                readU16Le(raw.data() + rowOffset + (static_cast<std::size_t>(x) * BYTES_PER_PIXEL));
        }
    }
    return true;
}

void BosonCamera::fillSyntheticFrame() {
    // Keep this frame fixed across opens and reads.  The values are raw-count
    // shaped data, not a temperature conversion.
    for (U32 y = 0U; y < HEIGHT; y++) {
        for (U32 x = 0U; x < WIDTH; x++) {
            const U32 dx = (x > 160U) ? (x - 160U) : (160U - x);
            const U32 dy = (y > 128U) ? (y - 128U) : (128U - y);
            const U32 hotspot = ((dx * dx) + (dy * dy) < 625U) ? 5000U : 0U;
            const U32 value = 1000U + (((x * 37U) + (y * 53U)) % 50000U) + hotspot;
            this->m_latestFrame[(y * WIDTH) + x] = static_cast<U16>(value);
        }
    }
}

BosonCamera::Status BosonCamera::openV4l2(char* reason, U32 reasonSize) {
#ifndef BOSON_USE_V4L2
    writeReason(reason, reasonSize, "v4l2 support is unavailable in this build");
    return DEVICE_ERROR;
#else
    const char* device = std::getenv(DEVICE_ENV);
    if ((device == nullptr) || (device[0] == '\0')) {
        writeReason(reason, reasonSize,
                    "BOSON_V4L2_DEVICE is required for v4l2 backend");
        return DEVICE_ERROR;
    }

    int openFlags = O_RDWR | O_NONBLOCK;
#ifdef O_CLOEXEC
    openFlags |= O_CLOEXEC;
#endif
    this->m_v4l2Fd = ::open(device, openFlags);
    if (this->m_v4l2Fd < 0) {
        char message[160] = {};
        std::snprintf(message, sizeof(message), "open %s: %s", device, std::strerror(errno));
        writeReason(reason, reasonSize, message);
        return DEVICE_ERROR;
    }

    v4l2_capability capability = {};
    if (ioctlRetry(this->m_v4l2Fd, VIDIOC_QUERYCAP, &capability) < 0) {
        char message[160] = {};
        std::snprintf(message, sizeof(message), "VIDIOC_QUERYCAP %s: %s", device, std::strerror(errno));
        writeReason(reason, reasonSize, message);
        this->closeV4l2();
        return DEVICE_ERROR;
    }
    const __u32 deviceCapabilities =
        (capability.capabilities & V4L2_CAP_DEVICE_CAPS) ? capability.device_caps : capability.capabilities;
    if ((deviceCapabilities & V4L2_CAP_VIDEO_CAPTURE) == 0U ||
        (deviceCapabilities & V4L2_CAP_STREAMING) == 0U) {
        writeReason(reason, reasonSize, "V4L2 device lacks capture/streaming capability");
        this->closeV4l2();
        return DEVICE_ERROR;
    }

    v4l2_format format = {};
    bool accepted = false;
    const U32 requestedHeights[] = {HEIGHT, HEIGHT + TELEMETRY_ROWS};
    for (const U32 requestedHeight : requestedHeights) {
        format = {};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = WIDTH;
        format.fmt.pix.height = requestedHeight;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_Y16;
        format.fmt.pix.field = V4L2_FIELD_NONE;
        if (ioctlRetry(this->m_v4l2Fd, VIDIOC_S_FMT, &format) < 0) {
            continue;
        }
        if ((format.fmt.pix.width != WIDTH) ||
            (format.fmt.pix.pixelformat != V4L2_PIX_FMT_Y16) ||
            ((format.fmt.pix.height != HEIGHT) &&
             (format.fmt.pix.height != HEIGHT + TELEMETRY_ROWS))) {
            continue;
        }

        this->m_v4l2Height = format.fmt.pix.height;
        this->m_v4l2Stride = (format.fmt.pix.bytesperline != 0U)
                                  ? format.fmt.pix.bytesperline
                                  : ROW_BYTES;
        if (this->m_v4l2Stride < ROW_BYTES) {
            this->m_v4l2Height = 0U;
            this->m_v4l2Stride = 0U;
            continue;
        }
        accepted = true;
        break;
    }
    if (!accepted) {
        writeReason(reason, reasonSize, "V4L2 requires strict Y16 320x256 or 320x258");
        this->closeV4l2();
        return FORMAT_ERROR;
    }

    v4l2_requestbuffers request = {};
    request.count = 4U;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (ioctlRetry(this->m_v4l2Fd, VIDIOC_REQBUFS, &request) < 0 || request.count < 2U) {
        writeReason(reason, reasonSize, "VIDIOC_REQBUFS MMAP failed or returned too few buffers");
        this->closeV4l2();
        return DEVICE_ERROR;
    }

    MmapBuffer* buffers = new (std::nothrow) MmapBuffer[request.count]{};
    if (buffers == nullptr) {
        writeReason(reason, reasonSize, "unable to allocate V4L2 buffer table");
        this->closeV4l2();
        return DEVICE_ERROR;
    }
    this->m_v4l2Buffers = buffers;
    this->m_v4l2BufferCount = request.count;

    for (U32 index = 0U; index < request.count; index++) {
        v4l2_buffer buffer = {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (ioctlRetry(this->m_v4l2Fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            writeReason(reason, reasonSize, "VIDIOC_QUERYBUF failed");
            this->closeV4l2();
            return DEVICE_ERROR;
        }

        buffers[index].length = buffer.length;
        buffers[index].start = ::mmap(nullptr,
                                      buffer.length,
                                      PROT_READ | PROT_WRITE,
                                      MAP_SHARED,
                                      this->m_v4l2Fd,
                                      static_cast<off_t>(buffer.m.offset));
        if (buffers[index].start == MAP_FAILED) {
            buffers[index].start = nullptr;
            writeReason(reason, reasonSize, "mmap V4L2 buffer failed");
            this->closeV4l2();
            return DEVICE_ERROR;
        }
    }

    for (U32 index = 0U; index < request.count; index++) {
        v4l2_buffer buffer = {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (ioctlRetry(this->m_v4l2Fd, VIDIOC_QBUF, &buffer) < 0) {
            writeReason(reason, reasonSize, "VIDIOC_QBUF failed");
            this->closeV4l2();
            return DEVICE_ERROR;
        }
    }

    v4l2_buf_type bufferType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctlRetry(this->m_v4l2Fd, VIDIOC_STREAMON, &bufferType) < 0) {
        writeReason(reason, reasonSize, "VIDIOC_STREAMON failed");
        this->closeV4l2();
        return DEVICE_ERROR;
    }

    this->m_v4l2StreamOn = true;
    this->m_streaming = true;
    writeReason(reason, reasonSize, "v4l2");
    return OK;
#endif
}

#ifdef BOSON_USE_V4L2

void BosonCamera::closeV4l2() {
    if ((this->m_v4l2Fd >= 0) && this->m_v4l2StreamOn) {
        v4l2_buf_type bufferType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        (void)ioctlRetry(this->m_v4l2Fd, VIDIOC_STREAMOFF, &bufferType);
    }
    this->m_v4l2StreamOn = false;

    MmapBuffer* buffers = static_cast<MmapBuffer*>(this->m_v4l2Buffers);
    if (buffers != nullptr) {
        for (U32 index = 0U; index < this->m_v4l2BufferCount; index++) {
            if ((buffers[index].start != nullptr) && (buffers[index].length > 0U)) {
                (void)::munmap(buffers[index].start, buffers[index].length);
            }
        }
        delete[] buffers;
    }
    this->m_v4l2Buffers = nullptr;
    this->m_v4l2BufferCount = 0U;
    this->m_v4l2Stride = 0U;
    this->m_v4l2Height = 0U;

    if (this->m_v4l2Fd >= 0) {
        (void)::close(this->m_v4l2Fd);
        this->m_v4l2Fd = -1;
    }
}

BosonCamera::Status BosonCamera::captureV4l2Frame(U16* out,
                                                  U32 timeoutMs,
                                                  char* reason,
                                                  U32 reasonSize) {
    if (this->m_v4l2Fd < 0 || !this->m_v4l2StreamOn) {
        writeReason(reason, reasonSize, "V4L2 stream is not active");
        return DEVICE_ERROR;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    // V4L2 dequeues the oldest queued buffer. Non-blockingly drain the buffers
    // that are already complete so a capture requested long after STREAMON
    // waits for a newly produced frame. Bound the drain because requeued
    // buffers may begin filling immediately.
    for (U32 drained = 0U; drained < this->m_v4l2BufferCount; drained++) {
        v4l2_buffer staleBuffer = {};
        staleBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        staleBuffer.memory = V4L2_MEMORY_MMAP;
        if (ioctlRetry(this->m_v4l2Fd, VIDIOC_DQBUF, &staleBuffer) < 0) {
            if (errno == EAGAIN) {
                break;
            }
            char message[128] = {};
            std::snprintf(message, sizeof(message), "VIDIOC_DQBUF while draining: %s", std::strerror(errno));
            writeReason(reason, reasonSize, message);
            return DEVICE_ERROR;
        }
        if ((staleBuffer.index >= this->m_v4l2BufferCount) ||
            !queueV4l2Buffer(this->m_v4l2Fd, staleBuffer.index)) {
            writeReason(reason, reasonSize, "failed to requeue stale V4L2 frame");
            return DEVICE_ERROR;
        }
    }

    bool rejectedPlaceholder = false;
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if ((timeoutMs != 0U) && (now >= deadline)) {
            writeReason(reason, reasonSize,
                        rejectedPlaceholder
                            ? "timed out waiting for usable V4L2 frame after startup placeholder"
                            : "timed out waiting for usable V4L2 frame");
            return FRAME_TIMEOUT;
        }
        const auto remaining = (now < deadline)
                                   ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()
                                   : 0;
        const int pollTimeout = (remaining > static_cast<long long>(std::numeric_limits<int>::max()))
                                    ? std::numeric_limits<int>::max()
                                    : static_cast<int>(remaining);

        pollfd descriptor = {};
        descriptor.fd = this->m_v4l2Fd;
        descriptor.events = POLLIN;
        const int pollResult = ::poll(&descriptor, 1U, pollTimeout);
        if (pollResult == 0) {
            writeReason(reason, reasonSize,
                        rejectedPlaceholder
                            ? "timed out waiting for usable V4L2 frame after startup placeholder"
                            : "timed out waiting for V4L2 frame");
            return FRAME_TIMEOUT;
        }
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            char message[128] = {};
            std::snprintf(message, sizeof(message), "poll V4L2 device: %s", std::strerror(errno));
            writeReason(reason, reasonSize, message);
            return DEVICE_ERROR;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            writeReason(reason, reasonSize, "V4L2 device reported an error");
            return DEVICE_ERROR;
        }

        v4l2_buffer buffer = {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (ioctlRetry(this->m_v4l2Fd, VIDIOC_DQBUF, &buffer) < 0) {
            if (errno == EAGAIN) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    writeReason(reason, reasonSize, "timed out waiting for V4L2 frame");
                    return FRAME_TIMEOUT;
                }
                continue;
            }
            char message[128] = {};
            std::snprintf(message, sizeof(message), "VIDIOC_DQBUF: %s", std::strerror(errno));
            writeReason(reason, reasonSize, message);
            return DEVICE_ERROR;
        }

        Status frameStatus = OK;
        bool retryFrame = (buffer.flags & V4L2_BUF_FLAG_ERROR) != 0U;
        if (buffer.index >= this->m_v4l2BufferCount) {
            writeReason(reason, reasonSize, "V4L2 returned an invalid buffer index");
            frameStatus = DEVICE_ERROR;
        } else if (retryFrame) {
            writeReason(reason, reasonSize, "V4L2 returned an error-marked frame");
        } else {
            MmapBuffer* buffers = static_cast<MmapBuffer*>(this->m_v4l2Buffers);
            const std::size_t mappedLength = buffers[buffer.index].length;
            const std::size_t bytesUsed = std::min<std::size_t>(buffer.bytesused, mappedLength);
            if ((this->m_v4l2Height != HEIGHT) &&
                (this->m_v4l2Height != HEIGHT + TELEMETRY_ROWS)) {
                writeReason(reason, reasonSize, "V4L2 returned an unsupported frame height");
                frameStatus = FORMAT_ERROR;
            } else {
                bool validByteRange = false;
                std::size_t requiredBytes = 0U;
                if ((this->m_v4l2Stride >= ROW_BYTES) && (this->m_v4l2Stride != 0U)) {
                    const std::size_t lastRow = static_cast<std::size_t>(this->m_v4l2Height - 1U);
                    const std::size_t maxSize = std::numeric_limits<std::size_t>::max();
                    if ((lastRow <= (maxSize / this->m_v4l2Stride))) {
                        const std::size_t lastRowOffset = lastRow * this->m_v4l2Stride;
                        if (lastRowOffset <= (maxSize - ROW_BYTES)) {
                            requiredBytes = lastRowOffset + ROW_BYTES;
                            validByteRange = (requiredBytes <= mappedLength) &&
                                             (requiredBytes <= bytesUsed);
                        }
                    }
                }
                if (!validByteRange) {
                    writeReason(reason, reasonSize,
                                "V4L2 frame is shorter than stride/bytesused require");
                    frameStatus = FORMAT_ERROR;
                } else {
                    const unsigned char* data = static_cast<const unsigned char*>(buffers[buffer.index].start);
                    const U32 firstImageRow =
                        (this->m_v4l2Height == HEIGHT + TELEMETRY_ROWS) ? TELEMETRY_ROWS : 0U;
                    for (U32 y = 0U; y < HEIGHT; y++) {
                        const std::size_t rowOffset =
                            static_cast<std::size_t>(y + firstImageRow) * this->m_v4l2Stride;
                        for (U32 x = 0U; x < WIDTH; x++) {
                            out[(y * WIDTH) + x] =
                                readU16Le(data + rowOffset + (static_cast<std::size_t>(x) * BYTES_PER_PIXEL));
                        }
                    }
                    if (isUsableFrame(out, NUM_PIXELS)) {
                        writeReason(reason, reasonSize, "v4l2");
                    } else {
                        writeReason(reason, reasonSize,
                                    "Boson returned an 0x8080 startup placeholder");
                        rejectedPlaceholder = true;
                        retryFrame = true;
                    }
                }
            }
        }

        if ((buffer.index < this->m_v4l2BufferCount) &&
            !queueV4l2Buffer(this->m_v4l2Fd, buffer.index)) {
            char message[128] = {};
            std::snprintf(message, sizeof(message), "VIDIOC_QBUF after capture: %s", std::strerror(errno));
            writeReason(reason, reasonSize, message);
            return DEVICE_ERROR;
        }
        if (retryFrame) {
            if (std::chrono::steady_clock::now() >= deadline) {
                writeReason(reason, reasonSize,
                            rejectedPlaceholder
                                ? "timed out waiting for usable V4L2 frame after startup placeholder"
                                : "timed out waiting for usable V4L2 frame");
                return FRAME_TIMEOUT;
            }
            continue;
        }
        return frameStatus;
    }
}

#endif  // BOSON_USE_V4L2

}  // namespace Components
