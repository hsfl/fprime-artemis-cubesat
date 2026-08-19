// ======================================================================
// \title  testBosonCamera.cpp
// \brief  Standalone, hardware-free BosonCamera regression harness.
//
// Example native build from the repository root:
//   c++ -std=c++17 -I ArtemisRpiTeensy_N2/lib/fprime
//       ArtemisRpiTeensy_N2/Components/PayloadDriver_Boson/BosonCamera.cpp
//       ArtemisRpiTeensy_N2/Components/PayloadDriver_Boson/testBosonCamera/testBosonCamera.cpp
//       -o /tmp/testBosonCamera
// ======================================================================

#include "../BosonCamera.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace {

void setEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    (void)_putenv_s(name, value);
#else
    (void)setenv(name, value, 1);
#endif
}

void clearEnvironment(const char* name) {
#ifdef _WIN32
    (void)_putenv_s(name, "");
#else
    (void)unsetenv(name);
#endif
}

bool contains(const char* text, const char* needle) {
    return (text != nullptr) && (needle != nullptr) && (std::strstr(text, needle) != nullptr);
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        return false;
    }
    return true;
}

U16 sampleValue(U32 x, U32 y) {
    return static_cast<U16>(0x1200U + (((x * 29U) + (y * 41U)) % 0x4000U));
}

bool writeRawSample(const std::string& path, U32 rows, bool telemetrySentinels) {
    std::ofstream file(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    for (U32 y = 0U; y < rows; y++) {
        for (U32 x = 0U; x < Components::BosonCamera::WIDTH; x++) {
            U16 value = 0U;
            if (telemetrySentinels && y < Components::BosonCamera::TELEMETRY_ROWS) {
                value = (y == 0U) ? 0xCAFEU : 0xBABEU;
            } else {
                const U32 imageRow = telemetrySentinels
                                          ? (y - Components::BosonCamera::TELEMETRY_ROWS)
                                          : y;
                value = sampleValue(x, imageRow);
            }
            const unsigned char bytes[2] = {
                static_cast<unsigned char>(value & 0xFFU),
                static_cast<unsigned char>((value >> 8U) & 0xFFU),
            };
            file.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
        }
    }
    return static_cast<bool>(file);
}

bool getFrame(Components::BosonCamera& camera, U16* frame) {
    char reason[192] = {};
    const Components::BosonCamera::Status status =
        camera.getLatestFrame(frame, Components::BosonCamera::NUM_PIXELS, 50U, reason, sizeof(reason));
    if (status != Components::BosonCamera::OK) {
        std::printf("FAIL: getLatestFrame status=%d reason=%s\n", static_cast<int>(status), reason);
        return false;
    }
    return true;
}

bool testSyntheticDeterminism() {
    setEnvironment("BOSON_CAMERA_BACKEND", "synthetic");
    clearEnvironment("BOSON_SAMPLE_FILE");
    clearEnvironment("BOSON_SAMPLE_PATH");
    clearEnvironment("BOSON_SAMPLE_RAW");

    Components::BosonCamera first;
    Components::BosonCamera second;
    char reason[192] = {};
    bool passed = true;
    passed &= expect(first.open(reason, sizeof(reason)) == Components::BosonCamera::OK,
                     "synthetic backend opens");
    passed &= expect(std::strcmp(first.backendName(), "synthetic") == 0,
                     "synthetic backend is selected");

    U16 firstFrame[Components::BosonCamera::NUM_PIXELS] = {};
    U16 repeatedFrame[Components::BosonCamera::NUM_PIXELS] = {};
    U16 secondFrame[Components::BosonCamera::NUM_PIXELS] = {};
    passed &= getFrame(first, firstFrame);
    passed &= getFrame(first, repeatedFrame);
    passed &= expect(std::memcmp(firstFrame, repeatedFrame, sizeof(firstFrame)) == 0,
                     "synthetic frame is stable across reads");

    std::memset(reason, 0, sizeof(reason));
    passed &= expect(second.open(reason, sizeof(reason)) == Components::BosonCamera::OK,
                     "second synthetic camera opens");
    passed &= getFrame(second, secondFrame);
    passed &= expect(std::memcmp(firstFrame, secondFrame, sizeof(firstFrame)) == 0,
                     "synthetic frame is stable across instances");
    passed &= expect(firstFrame[0] != 0U && firstFrame[0] != 0xCAFEU,
                     "synthetic frame contains raw counts");
    first.close();
    second.close();
    return passed;
}

bool testFrameVariationValidation() {
    U16 frame[Components::BosonCamera::NUM_PIXELS] = {};
    for (U32 index = 0U; index < Components::BosonCamera::NUM_PIXELS; index++) {
        frame[index] = 0x8080U;
    }

    bool passed = expect(
        !Components::BosonCamera::isUsableFrame(
            frame, Components::BosonCamera::NUM_PIXELS),
        "flat startup frame is rejected");

    // Reproduce the sparse paired-byte pattern observed in the bad HIL FDP.
    frame[(12U * Components::BosonCamera::WIDTH) + 295U] = 0x3737U;
    frame[(140U * Components::BosonCamera::WIDTH) + 295U] = 0x2323U;
    passed &= expect(
        !Components::BosonCamera::isUsableFrame(
            frame, Components::BosonCamera::NUM_PIXELS),
        "sparse Boson startup/test pattern is rejected");

    const U32 rejectCount =
        static_cast<U32>(
            ((static_cast<U64>(Components::BosonCamera::NUM_PIXELS) * 99U) + 99U) /
            100U);
    for (U32 index = 0U; index < Components::BosonCamera::NUM_PIXELS; index++) {
        frame[index] = (index < rejectCount) ? 0x8080U : 10000U;
    }
    passed &= expect(
        !Components::BosonCamera::isUsableFrame(
            frame, Components::BosonCamera::NUM_PIXELS),
        "99-percent startup placeholder frame is rejected");
    frame[rejectCount - 1U] = 10000U;
    passed &= expect(
        Components::BosonCamera::isUsableFrame(
            frame, Components::BosonCamera::NUM_PIXELS),
        "frame below the placeholder threshold is accepted");

    for (U32 index = 0U; index < Components::BosonCamera::NUM_PIXELS; index++) {
        frame[index] = 10000U;
    }
    passed &= expect(
        Components::BosonCamera::isUsableFrame(
            frame, Components::BosonCamera::NUM_PIXELS),
        "uniform non-placeholder thermal counts are accepted");

    for (U32 index = 0U; index < Components::BosonCamera::NUM_PIXELS; index++) {
        frame[index] = static_cast<U16>(10000U + ((index * 37U) % 4096U));
    }
    passed &= expect(
        Components::BosonCamera::isUsableFrame(
            frame, Components::BosonCamera::NUM_PIXELS),
        "thermal-like raw-count variation is accepted");
    passed &= expect(
        !Components::BosonCamera::isUsableFrame(nullptr, 0U),
        "missing frame is rejected");
    return passed;
}

bool testSample256(const std::string& path) {
    if (!writeRawSample(path, Components::BosonCamera::HEIGHT, false)) {
        std::printf("FAIL: write 256-row sample\n");
        return false;
    }
    setEnvironment("BOSON_CAMERA_BACKEND", "sample");
    setEnvironment("BOSON_SAMPLE_FILE", path.c_str());

    Components::BosonCamera camera;
    char reason[192] = {};
    bool passed = expect(camera.open(reason, sizeof(reason)) == Components::BosonCamera::OK,
                         "256-row sample opens");
    U16 frame[Components::BosonCamera::NUM_PIXELS] = {};
    if (passed) {
        passed &= getFrame(camera, frame);
        passed &= expect(frame[0] == sampleValue(0U, 0U), "256-row sample keeps first pixel");
        passed &= expect(frame[Components::BosonCamera::NUM_PIXELS - 1U] == sampleValue(319U, 255U),
                         "256-row sample keeps last pixel");
    }
    camera.close();
    return passed;
}

bool testSample258(const std::string& path) {
    if (!writeRawSample(path, Components::BosonCamera::HEIGHT + Components::BosonCamera::TELEMETRY_ROWS, true)) {
        std::printf("FAIL: write 258-row sample\n");
        return false;
    }
    setEnvironment("BOSON_CAMERA_BACKEND", "sample");
    setEnvironment("BOSON_SAMPLE_FILE", path.c_str());

    Components::BosonCamera camera;
    char reason[192] = {};
    bool passed = expect(camera.open(reason, sizeof(reason)) == Components::BosonCamera::OK,
                         "258-row sample opens");
    U16 frame[Components::BosonCamera::NUM_PIXELS] = {};
    if (passed) {
        passed &= getFrame(camera, frame);
        passed &= expect(frame[0] == sampleValue(0U, 0U),
                         "258-row sample strips exactly the first telemetry row pair");
        passed &= expect(frame[319U] == sampleValue(319U, 0U),
                         "258-row sample preserves first image row");
        passed &= expect(frame[Components::BosonCamera::NUM_PIXELS - 1U] == sampleValue(319U, 255U),
                         "258-row sample preserves last image row");
        passed &= expect(frame[0] != 0xCAFEU && frame[319U] != 0xBABEU,
                         "258-row telemetry sentinels are not returned");
    }
    camera.close();
    return passed;
}

bool testMalformedSample(const std::string& path) {
    std::ofstream file(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file) {
        std::printf("FAIL: create malformed sample\n");
        return false;
    }
    const unsigned char byte = 0x42U;
    file.write(reinterpret_cast<const char*>(&byte), 1);
    file.close();

    setEnvironment("BOSON_CAMERA_BACKEND", "sample");
    setEnvironment("BOSON_SAMPLE_FILE", path.c_str());
    Components::BosonCamera camera;
    char reason[192] = {};
    const Components::BosonCamera::Status status = camera.open(reason, sizeof(reason));
    const bool passed = expect(status == Components::BosonCamera::SAMPLE_ERROR,
                               "malformed sample is rejected") &&
                        expect(!camera.isStreaming(), "malformed sample does not start streaming");
    camera.close();
    return passed;
}

bool testInvalidBackend() {
    setEnvironment("BOSON_CAMERA_BACKEND", "not-a-backend");
    Components::BosonCamera camera;
    char reason[192] = {};
    const Components::BosonCamera::Status status = camera.open(reason, sizeof(reason));
    const bool passed = expect(status == Components::BosonCamera::INVALID_BACKEND,
                               "invalid backend is rejected") &&
                        expect(contains(reason, "BOSON_CAMERA_BACKEND"),
                               "invalid backend reason names the environment variable");
    camera.close();
    return passed;
}

bool testDefaultAndV4l2Failure() {
    clearEnvironment("BOSON_CAMERA_BACKEND");
    clearEnvironment("BOSON_V4L2_DEVICE");
    clearEnvironment("BOSON_SAMPLE_FILE");
    clearEnvironment("BOSON_SAMPLE_PATH");
    clearEnvironment("BOSON_SAMPLE_RAW");

    Components::BosonCamera defaultCamera;
    char reason[192] = {};
    const Components::BosonCamera::Status defaultStatus = defaultCamera.open(reason, sizeof(reason));
#ifdef __linux__
    bool passed = expect(defaultStatus != Components::BosonCamera::OK,
                         "Linux default does not silently produce synthetic data") &&
                  expect(contains(reason, "BOSON_V4L2_DEVICE"),
                         "Linux default failure requires an explicit V4L2 device");
#else
    bool passed = expect(defaultStatus == Components::BosonCamera::OK,
                         "non-Linux default is synthetic");
#endif
    defaultCamera.close();

    setEnvironment("BOSON_CAMERA_BACKEND", "v4l2");
    setEnvironment("BOSON_V4L2_DEVICE", "/dev/null");
    Components::BosonCamera v4l2Camera;
    std::memset(reason, 0, sizeof(reason));
    const Components::BosonCamera::Status v4l2Status = v4l2Camera.open(reason, sizeof(reason));
    passed &= expect(v4l2Status != Components::BosonCamera::OK,
                     "V4L2 backend fails without a real capture device");
    passed &= expect(!v4l2Camera.isStreaming(), "failed V4L2 open is not streaming");
    v4l2Camera.close();
    return passed;
}

}  // namespace

int main() {
    const std::string prefix = "boson_camera_test_" +
                               std::to_string(static_cast<unsigned long long>(
                                   std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const std::string sample256 = prefix + "_256.raw";
    const std::string sample258 = prefix + "_258.raw";
    const std::string malformed = prefix + "_malformed.raw";

    int failures = 0;
    failures += testSyntheticDeterminism() ? 0 : 1;
    failures += testFrameVariationValidation() ? 0 : 1;
    failures += testSample256(sample256) ? 0 : 1;
    failures += testSample258(sample258) ? 0 : 1;
    failures += testMalformedSample(malformed) ? 0 : 1;
    failures += testInvalidBackend() ? 0 : 1;
    failures += testDefaultAndV4l2Failure() ? 0 : 1;

    (void)std::remove(sample256.c_str());
    (void)std::remove(sample258.c_str());
    (void)std::remove(malformed.c_str());
    clearEnvironment("BOSON_CAMERA_BACKEND");
    clearEnvironment("BOSON_V4L2_DEVICE");
    clearEnvironment("BOSON_SAMPLE_FILE");
    clearEnvironment("BOSON_SAMPLE_PATH");
    clearEnvironment("BOSON_SAMPLE_RAW");

    if (failures != 0) {
        std::printf("BosonCamera tests FAILED (%d test groups)\n", failures);
        return 1;
    }
    std::printf("BosonCamera tests passed: synthetic, frame validation, 256-row sample, 258-row stripping, malformed sample, invalid backend, V4L2 failure\n");
    return 0;
}
