#include "Components/StorageManager/StorageManager.hpp"
#include "Components/StorageManager/StorageManagerGTestBase.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace Components {
namespace {

constexpr char TEST_BYTES[] = "123456789";
constexpr U32 TEST_SIZE = sizeof(TEST_BYTES) - 1U;
constexpr U32 TEST_CRC = 0x29B1U;  // CRC16-CCITT-FALSE check value.

class TestDirectory {
  public:
    TestDirectory() {
        const char* const prior = std::getenv("N2_STORAGE_ROOT");
        this->hadPriorRoot = prior != nullptr;
        if (this->hadPriorRoot) {
            this->priorRoot = prior;
        }
        char pattern[] = "./storage_manager_ut_XXXXXX";
        const char* const path = ::mkdtemp(pattern);
        if (path != nullptr) {
            this->root = path;
            this->configured = ::setenv("N2_STORAGE_ROOT", path, 1) == 0;
        }
    }

    ~TestDirectory() {
        if (this->configured) {
            if (this->hadPriorRoot) {
                (void)::setenv("N2_STORAGE_ROOT", this->priorRoot.c_str(), 1);
            } else {
                (void)::unsetenv("N2_STORAGE_ROOT");
            }
        }
        if (!this->root.empty()) {
            for (const char* name : {"source.bin", "product_17.bin", "product_17.meta", "product_18.bin",
                                     "product_18.meta", "product-id-high-water", "product-id.lock"}) {
                (void)::unlink(this->path(name).c_str());
            }
            (void)::rmdir(this->root.c_str());
        }
    }

    std::string path(const char* name) const { return this->root + "/" + name; }
    bool configured = false;

  private:
    std::string root;
    std::string priorRoot;
    bool hadPriorRoot = false;
};

}  // namespace

class StorageManagerTester final : public StorageManagerGTestBase {
  public:
    StorageManagerTester() : StorageManagerGTestBase("StorageManagerTester", 32), component("StorageManager") {
        this->init();
        this->component.init(10, 0);
        this->connect_to_requestIn(0, this->component.get_requestIn_InputPort(0));
        this->connect_to_deliveryRequestIn(0, this->component.get_deliveryRequestIn_InputPort(0));
        this->component.set_productCommitOut_OutputPort(0, this->get_from_productCommitOut(0));
        this->component.set_downlinkReadyOut_OutputPort(0, this->get_from_downlinkReadyOut(0));
        this->component.set_deliveryDescriptorOut_OutputPort(0, this->get_from_deliveryDescriptorOut(0));
        this->component.set_logOut_OutputPort(0, this->get_from_logOut(0));
#if FW_ENABLE_TEXT_LOGGING == 1
        this->component.set_logTextOut_OutputPort(0, this->get_from_logTextOut(0));
#endif
        this->component.set_tlmOut_OutputPort(0, this->get_from_tlmOut(0));
        this->component.set_timeCaller_OutputPort(0, this->get_from_timeCaller(0));
    }

    ~StorageManagerTester() { this->component.deinit(); }

    void testCommitAndDeliverExactProduct() {
        ASSERT_TRUE(this->directory.configured);
        ASSERT_TRUE(this->writeSource());
        this->submit(17U, TEST_CRC);
        this->submit(18U, TEST_CRC);
        ASSERT_from_productCommitOut_SIZE(2);
        ASSERT_from_productCommitOut(0, 17U, TEST_SIZE, true);
        ASSERT_from_productCommitOut(1, 18U, TEST_SIZE, true);
        ASSERT_EVENTS_ScienceProductCommitted_SIZE(2);

        // A newer commit may be selected before the asynchronous delivery
        // request arrives. The descriptor must still identify the requested ID.
        this->invoke_to_deliveryRequestIn(0, 17U);
        this->component.doDispatch();
        const std::string storedPath = this->directory.path("product_17.bin");
        ASSERT_from_deliveryDescriptorOut_SIZE(1);
        ASSERT_from_deliveryDescriptorOut(0, 17U, TEST_SIZE, ScienceProductSource::TEST,
                                          Fw::String(storedPath.c_str()), TEST_CRC);
        std::ifstream stored(storedPath, std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(stored), std::istreambuf_iterator<char>()};
        ASSERT_EQ(contents, TEST_BYTES);
    }

    void testRejectBadCrc() {
        ASSERT_TRUE(this->directory.configured);
        ASSERT_TRUE(this->writeSource());
        this->submit(17U, TEST_CRC ^ 1U);
        ASSERT_from_productCommitOut_SIZE(1);
        ASSERT_from_productCommitOut(0, 17U, 0U, false);
        ASSERT_EVENTS_ScienceProductCommitted_SIZE(0);
        ASSERT_from_downlinkReadyOut_SIZE(0);
        ASSERT_NE(::access(this->directory.path("product_17.bin").c_str(), F_OK), 0);
        ASSERT_NE(::access(this->directory.path("product_17.meta").c_str(), F_OK), 0);

        this->invoke_to_deliveryRequestIn(0, 17U);
        this->component.doDispatch();
        ASSERT_from_deliveryDescriptorOut_SIZE(0);
        ASSERT_from_productCommitOut_SIZE(1);
    }

  private:
    bool writeSource() const {
        std::ofstream source(this->directory.path("source.bin"), std::ios::binary);
        source.write(TEST_BYTES, TEST_SIZE);
        source.close();
        return source.good();
    }

    void submit(U32 productId, U32 crc) {
        const Fw::String source(this->directory.path("source.bin").c_str());
        this->invoke_to_requestIn(0, productId, TEST_SIZE, ScienceProductSource::TEST, source, crc);
        this->component.doDispatch();
    }

    TestDirectory directory;
    StorageManager component;
};

TEST(StorageManagerCommit, CommitAndDeliverExactProduct) {
    StorageManagerTester tester;
    tester.testCommitAndDeliverExactProduct();
}

TEST(StorageManagerCommit, RejectBadCrc) {
    StorageManagerTester tester;
    tester.testRejectBadCrc();
}

}  // namespace Components

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
