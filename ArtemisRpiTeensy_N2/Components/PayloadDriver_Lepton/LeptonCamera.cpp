#include "Components/PayloadDriver_Lepton/LeptonCamera.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Components {

LeptonCamera::LeptonCamera() : m_streaming(false), m_frameCounter(0), m_latestFrame{} {}

LeptonCamera::~LeptonCamera() {
    this->close();
}

LeptonCamera::Status LeptonCamera::open(char* reason, U32 reasonSize) {
    this->m_streaming = true;
    this->fillSyntheticFrame();
    writeReason(reason, reasonSize, "using deterministic local Lepton thermal stub");
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

    this->fillSyntheticFrame();
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
}

void LeptonCamera::writeReason(char* reason, U32 reasonSize, const char* message) {
    if ((reason != nullptr) && (reasonSize > 0U)) {
        std::snprintf(reason, reasonSize, "%s", message);
    }
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
