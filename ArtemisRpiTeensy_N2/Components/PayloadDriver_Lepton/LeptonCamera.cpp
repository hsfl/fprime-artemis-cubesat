#include "Components/PayloadDriver_Lepton/LeptonCamera.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Components {

LeptonCamera::LeptonCamera() : m_streaming(false), m_usingSampleFrame(false), m_frameCounter(0), m_latestFrame{} {}

LeptonCamera::~LeptonCamera() {
    this->close();
}

LeptonCamera::Status LeptonCamera::open(char* reason, U32 reasonSize) {
    this->m_streaming = true;
    if (this->loadSampleFrame(reason, reasonSize)) {
        this->m_usingSampleFrame = true;
        return OK;
    }
    this->m_usingSampleFrame = false;
    this->fillSyntheticFrame();
    writeReason(reason, reasonSize, "using fallback deterministic local Lepton thermal stub");
    return OK;
}

LeptonCamera::Status LeptonCamera::getLatestFrame(U16* out,
                                                  U32 numPixels,
                                                  U32 timeoutMs,
                                                  char* reason,
                                                  U32 reasonSize) {
    static_cast<void>(timeoutMs);
    if (!this->m_streaming) {
        writeReason(reason, reasonSize, "camera not streaming (ENABLE first)");
        return STREAM_NOT_READY;
    }
    if ((out == nullptr) || (numPixels != NUM_PIXELS)) {
        writeReason(reason, reasonSize, "invalid Lepton output buffer");
        return DEVICE_ERROR;
    }

    if (!this->m_usingSampleFrame) {
        this->fillSyntheticFrame();
    }
    std::memcpy(out, this->m_latestFrame, sizeof(this->m_latestFrame));
    return OK;
}

void LeptonCamera::close() {
    this->m_streaming = false;
}

bool LeptonCamera::isStreaming() const {
    return this->m_streaming;
}

void LeptonCamera::ingestFrameRaw(const void* data, U32 numPixels) {
    if ((data == nullptr) || (numPixels != NUM_PIXELS)) {
        return;
    }
    std::memcpy(this->m_latestFrame, data, sizeof(this->m_latestFrame));
    this->m_streaming = true;
    this->m_usingSampleFrame = true;
}

void LeptonCamera::writeReason(char* reason, U32 reasonSize, const char* message) {
    if ((reason != nullptr) && (reasonSize > 0U)) {
        std::snprintf(reason, reasonSize, "%s", message);
    }
}

bool LeptonCamera::loadSampleFrame(char* reason, U32 reasonSize) {
    const char* envPath = std::getenv("C3M_LEPTON_SAMPLE_CSV");
    if ((envPath != nullptr) && (envPath[0] != '\0') && this->loadSampleCsv(envPath)) {
        writeReason(reason, reasonSize, "using EPSCoR C3M Lepton sample CSV");
        return true;
    }

    const char* candidates[] = {
        "../ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv",
        "ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv",
        "../../ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv",
    };
    for (const char* candidate : candidates) {
        if (this->loadSampleCsv(candidate)) {
            writeReason(reason, reasonSize, "using EPSCoR C3M Lepton sample CSV");
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
            this->m_latestFrame[count] = static_cast<U16>(std::lround(centikelvin));
            count += 1U;
            cursor = end;
        }
    }

    std::fclose(file);
    return count == NUM_PIXELS;
}

void LeptonCamera::fillSyntheticFrame() {
    this->m_frameCounter += 1U;
    constexpr U16 AMBIENT_CK = 29415U;
    constexpr U16 HOTSPOT_CK = 850U;

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
            this->m_latestFrame[index] = value;
        }
    }
}

}  // namespace Components
