#include "StorageManagerTester.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace Components {

namespace {
constexpr const char* SOURCE_PATH = "/tmp/storage_manager_ut_source.bin";
constexpr U8 PRODUCT[] = {1U, 2U, 3U, 4U};

// N2_STORAGE_ROOT is read when the component is built, so this runs first, and each test gets a clean folder.
const char* freshStorageName() {
    (void)std::system("rm -rf /tmp/storage_manager_ut_root");
    (void)::setenv("N2_STORAGE_ROOT", "/tmp/storage_manager_ut_root", 1);
    return "StorageManager";
}

U32 productCrc() {
    U16 crc = 0xFFFFU;
    for (const U8 value : PRODUCT) {
        crc ^= static_cast<U16>(value) << 8U;
        for (U8 bit = 0; bit < 8U; ++bit) {
            crc = ((crc & 0x8000U) != 0U) ? static_cast<U16>((crc << 1U) ^ 0x1021U) : static_cast<U16>(crc << 1U);
        }
    }
    return crc;
}

bool writeSource() {
    std::FILE* file = std::fopen(SOURCE_PATH, "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t written = std::fwrite(PRODUCT, 1U, sizeof(PRODUCT), file);
    return (std::fclose(file) == 0) && (written == sizeof(PRODUCT));
}
}  // namespace

StorageManagerTester::StorageManagerTester()
    : StorageManagerGTestBase("StorageManagerTester", MAX_HISTORY_SIZE),
      component(freshStorageName()) {
    this->initComponents();
    this->connectPorts();
}

StorageManagerTester::~StorageManagerTester() {
    this->component.deinit();
    (void)::unlink(SOURCE_PATH);
    (void)std::system("rm -rf /tmp/storage_manager_ut_root");
}

void StorageManagerTester::testCommittedProductIsReported() {
    this->clearHistory();
    ASSERT_TRUE(writeSource());
    this->invoke_to_requestIn(0, 5U, sizeof(PRODUCT), Components::ScienceProductSource::NEUTRON_SIM,
                              Fw::String(SOURCE_PATH), productCrc());
    this->component.doDispatch();
    ASSERT_EVENTS_ScienceProductCommitted_SIZE(1);
    ASSERT_from_productStoredOut_SIZE(1);
    ASSERT_from_productStoredOut(0, 5U, true);
}

void StorageManagerTester::testBadChecksumIsReportedNotStored() {
    this->clearHistory();
    ASSERT_TRUE(writeSource());
    this->invoke_to_requestIn(0, 6U, sizeof(PRODUCT), Components::ScienceProductSource::NEUTRON_SIM,
                              Fw::String(SOURCE_PATH), productCrc() ^ 0x1U);
    this->component.doDispatch();
    ASSERT_EVENTS_ScienceProductCommitted_SIZE(0);
    ASSERT_EVENTS_ScienceProductRejected_SIZE(1);
    ASSERT_from_productStoredOut_SIZE(1);
    ASSERT_from_productStoredOut(0, 6U, false);
}

void StorageManagerTester::testBadDescriptorIsReportedNotStored() {
    this->clearHistory();
    this->invoke_to_requestIn(0, 7U, 0U, Components::ScienceProductSource::UNKNOWN, Fw::String(""), 0U);
    this->component.doDispatch();
    ASSERT_from_productStoredOut_SIZE(1);
    ASSERT_from_productStoredOut(0, 7U, false);
}

}  // namespace Components
