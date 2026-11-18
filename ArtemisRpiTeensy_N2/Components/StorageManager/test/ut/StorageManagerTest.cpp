#include "StorageManagerTester.hpp"
#include "Components/ProductId/ProductId.hpp"

#include <cstdlib>
#include <dirent.h>
#include <memory>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
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
            DIR* directory = ::opendir(this->root.c_str());
            if (directory != nullptr) {
                while (const struct dirent* entry = ::readdir(directory)) {
                    if (entry->d_name[0] != '.') {
                        (void)::unlink(this->path(entry->d_name).c_str());
                    }
                }
                (void)::closedir(directory);
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

class StorageManagerTester::CatalogTester final : public StorageManagerGTestBase {
  public:
    CatalogTester() : StorageManagerGTestBase("StorageCatalogTester", 128), component("StorageManager") {
        this->init();
        this->initialize(this->component);
    }

    void initialize(StorageManager& target) {
        target.init(10, 0);
        this->connect_to_requestIn(0, target.get_requestIn_InputPort(0));
        this->connect_to_provenanceIn(0, target.get_provenanceIn_InputPort(0));
        this->connect_to_deliveryRequestIn(0, target.get_deliveryRequestIn_InputPort(0));
        this->connect_to_run(0, target.get_run_InputPort(0));
        this->connect_to_cmdIn(0, target.get_cmdIn_InputPort(0));
        target.set_cmdRegOut_OutputPort(0, this->get_from_cmdRegOut(0));
        target.set_cmdResponseOut_OutputPort(0, this->get_from_cmdResponseOut(0));
        target.set_capacityOut_OutputPort(0, this->get_from_capacityOut(0));
        target.set_productCommitOut_OutputPort(0, this->get_from_productCommitOut(0));
        target.set_downlinkReadyOut_OutputPort(0, this->get_from_downlinkReadyOut(0));
        target.set_deliveryDescriptorOut_OutputPort(0, this->get_from_deliveryDescriptorOut(0));
        target.set_logOut_OutputPort(0, this->get_from_logOut(0));
#if FW_ENABLE_TEXT_LOGGING == 1
        target.set_logTextOut_OutputPort(0, this->get_from_logTextOut(0));
#endif
        target.set_tlmOut_OutputPort(0, this->get_from_tlmOut(0));
        target.set_timeCaller_OutputPort(0, this->get_from_timeCaller(0));
    }

    ~CatalogTester() { this->component.deinit(); }

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

    void testProvenanceNamesAndRecovery() {
        ASSERT_TRUE(this->directory.configured);
        ASSERT_TRUE(this->writeSource());
        std::ofstream csvSource(this->directory.path("source.csv"), std::ios::binary);
        csvSource.write(TEST_BYTES, TEST_SIZE);
        csvSource.close();
        ASSERT_TRUE(csvSource.good());
        const U32 acquisitions[] = {1113U, 1113U, 1113U, 1114U, 1113U};
        const U32 runs[] = {0U, 0U, 1U, 0U, 0U};
        const char* extensions[] = {"bin", "csv", "bin", "csv", "csv"};
        for (U32 index = 0U; index < 5U; ++index) {
            const U32 productId = 17U + index;
            this->invoke_to_provenanceIn(0, productId, acquisitions[index], runs[index]);
            const std::string sourceName = std::string("source.") + extensions[index];
            const Fw::String source(this->directory.path(sourceName.c_str()).c_str());
            // Metadata and descriptor use the same asynchronous FIFO queue.
            this->invoke_to_requestIn(0, productId, TEST_SIZE, ScienceProductSource::TEST, source, TEST_CRC);
            this->component.doDispatch();
            this->component.doDispatch();
            const std::string basename = "daq_" + std::to_string(acquisitions[index]) + "_run_" +
                std::to_string(runs[index]) + "_product_" + std::to_string(productId) + "." + extensions[index];
            ASSERT_EQ(this->readFile(basename.c_str()), TEST_BYTES);
            const std::string metadataName = "product_" + std::to_string(productId) + ".meta";
            const std::string expectedMetadata = "N2P2 " + std::to_string(productId) + " 9 " +
                std::to_string(static_cast<U32>(ScienceProductSource::TEST)) + " 10673 " +
                std::to_string(acquisitions[index]) + " " + std::to_string(runs[index]) + "\n";
            ASSERT_EQ(this->readFile(metadataName.c_str()), expectedMetadata);
        }
        // Legacy unlabelled products keep their names and N2P1 metadata.
        this->submit(22U, TEST_CRC);
        ASSERT_EQ(this->readFile("product_22.bin"), TEST_BYTES);
        ASSERT_EQ(this->readFile("product_22.meta").substr(0, 4), "N2P1");
        ASSERT_from_productCommitOut_SIZE(6);
        ASSERT_EVENTS_ProductProvenance_SIZE(5);
        ASSERT_EVENTS_ProductProvenance(0, 17U, 1113U, 0U);
        ASSERT_EVENTS_ProductProvenance(1, 18U, 1113U, 0U);
        ASSERT_EVENTS_ProductProvenance(2, 19U, 1113U, 1U);
        ASSERT_EVENTS_ProductProvenance(3, 20U, 1114U, 0U);
        ASSERT_EVENTS_ProductProvenance(4, 21U, 1113U, 0U);

        this->clearHistory();
        auto destroy = [](StorageManager* value) { value->deinit(); delete value; };
        std::unique_ptr<StorageManager, decltype(destroy)> recovered(new StorageManager("Recovered"), destroy);
        this->initialize(*recovered);
        this->invoke_to_deliveryRequestIn(0, 18U);
        recovered->doDispatch();
        ASSERT_EVENTS_ScienceProductRecovered_SIZE(6);
        ASSERT_EVENTS_ProductProvenance_SIZE(5);
        const std::string csvPath = this->directory.path("daq_1113_run_0_product_18.csv");
        ASSERT_from_deliveryDescriptorOut_SIZE(1);
        ASSERT_from_deliveryDescriptorOut(0, 18U, TEST_SIZE, ScienceProductSource::TEST,
                                          Fw::String(csvPath.c_str()), TEST_CRC);
        this->invoke_to_deliveryRequestIn(0, 22U);
        recovered->doDispatch();
        const std::string legacyPath = this->directory.path("product_22.bin");
        ASSERT_from_deliveryDescriptorOut(1, 22U, TEST_SIZE, ScienceProductSource::TEST,
                                          Fw::String(legacyPath.c_str()), TEST_CRC);
    }

    void testRejectMismatchedRecoveredProvenance() {
        ASSERT_TRUE(this->directory.configured);
        ASSERT_TRUE(this->writeSource());
        this->invoke_to_provenanceIn(0, 17U, 1113U, 0U);
        this->component.doDispatch();
        this->submit(17U, TEST_CRC);
        ASSERT_from_productCommitOut(0, 17U, TEST_SIZE, true);
        const std::string metadata = this->directory.path("product_17.meta");
        ASSERT_EQ(::chmod(metadata.c_str(), 0640), 0);
        std::ofstream changed(metadata);
        changed << "N2P2 17 9 " << static_cast<U32>(ScienceProductSource::TEST) << " 10673 1114 0\n";
        changed.close();
        ASSERT_TRUE(changed.good());
        this->clearHistory();
        auto destroy = [](StorageManager* value) { value->deinit(); delete value; };
        std::unique_ptr<StorageManager, decltype(destroy)> recovered(new StorageManager("Recovered"), destroy);
        this->initialize(*recovered);
        this->invoke_to_deliveryRequestIn(0, 17U);
        recovered->doDispatch();
        ASSERT_EVENTS_ScienceProductRecovered_SIZE(0);
        ASSERT_EVENTS_ProductProvenance_SIZE(0);
        ASSERT_from_deliveryDescriptorOut_SIZE(0);
        ASSERT_EQ(this->readFile("daq_1113_run_0_product_17.bin"), TEST_BYTES);
    }

    void testConflictingAndConsumedProvenance() {
        ASSERT_TRUE(this->directory.configured);
        ASSERT_TRUE(this->writeSource());
        this->invoke_to_provenanceIn(0, 17U, 1113U, 0U);
        this->component.doDispatch();
        this->invoke_to_provenanceIn(0, 17U, 1114U, 0U);
        this->component.doDispatch();
        this->submit(17U, TEST_CRC);
        ASSERT_from_productCommitOut(0, 17U, 0U, false);
        ASSERT_EVENTS_ProductProvenanceRejected_SIZE(1);
        this->submit(17U, TEST_CRC);
        ASSERT_from_productCommitOut(1, 17U, TEST_SIZE, true);
        ASSERT_EQ(this->readFile("product_17.bin"), TEST_BYTES);
        ASSERT_EVENTS_ProductProvenance_SIZE(0);
    }

    void testProvenanceCapacityFailsClosed() {
        ASSERT_TRUE(this->directory.configured);
        ASSERT_TRUE(this->writeSource());
        for (U32 productId = 1U; productId <= 17U; ++productId) {
            this->invoke_to_provenanceIn(0, productId, 1113U, 0U);
            this->component.doDispatch();
        }
        this->submit(17U, TEST_CRC);
        ASSERT_from_productCommitOut(0, 17U, 0U, false);
        ASSERT_EVENTS_ProductProvenanceRejected_SIZE(1);
        ASSERT_EVENTS_ProductProvenanceRejected(0, 17U, 3U);
        ASSERT_EVENTS_ScienceProductCommitted_SIZE(0);
    }

    void testOrphanProvenanceReservesId() {
        ASSERT_TRUE(this->directory.configured);
        std::ofstream orphan(this->directory.path("daq_1113_run_7_product_90.csv"));
        orphan << TEST_BYTES;
        orphan.close();
        U32 reserved = 0U;
        ASSERT_TRUE(reserveProductId(reserved));
        ASSERT_EQ(reserved, 91U);
        ASSERT_TRUE(reserveProductId(reserved));
        ASSERT_EQ(reserved, 92U);
        ASSERT_TRUE(this->writeSource());
        this->submit(90U, TEST_CRC);
        ASSERT_from_productCommitOut(0, 90U, 0U, false);
        ASSERT_EQ(this->readFile("daq_1113_run_7_product_90.csv"), TEST_BYTES);
    }

    void testCapacityAndExplicitRetirement() {
        ASSERT_TRUE(this->writeSource());
        this->invoke_to_run(0, 0U);
        this->component.doDispatch();
        ASSERT_from_capacityOut_SIZE(1);
        ASSERT_from_capacityOut(0, 16U, true);
        // A repeated status tick does not fill the private admission queues.
        this->invoke_to_run(0, 0U);
        this->component.doDispatch();
        ASSERT_from_capacityOut_SIZE(1);
        for (U32 productId = 1U; productId <= 16U; ++productId) {
            this->submit(productId, TEST_CRC);
        }
        ASSERT_from_capacityOut_SIZE(17);
        ASSERT_from_capacityOut(16, 0U, true);
        this->submit(17U, TEST_CRC);
        ASSERT_from_productCommitOut(16, 17U, 0U, false);

        this->clearHistory();
        this->sendCmd_RETIRE_PRODUCT(0, 0U, 16U, 0U);
        this->component.doDispatch();
        ASSERT_CMD_RESPONSE(0, StorageManagerComponentBase::OPCODE_RETIRE_PRODUCT, 0U, Fw::CmdResponse::OK);
        ASSERT_EVENTS_ProductRetirementPreview(0, 16U, TEST_SIZE,
            this->directory.path("product_16.bin").c_str());
        ASSERT_EVENTS_ProductRetired_SIZE(0);
        ASSERT_EQ(this->readFile("product_16.bin"), TEST_BYTES);
        this->sendCmd_RETIRE_PRODUCT(0, 1U, 16U, 2U);
        this->component.doDispatch();
        ASSERT_CMD_RESPONSE(1, StorageManagerComponentBase::OPCODE_RETIRE_PRODUCT, 1U,
                            Fw::CmdResponse::VALIDATION_ERROR);
        this->sendCmd_RETIRE_PRODUCT(0, 2U, 16U, 1U);
        this->component.doDispatch();
        ASSERT_CMD_RESPONSE(2, StorageManagerComponentBase::OPCODE_RETIRE_PRODUCT, 2U, Fw::CmdResponse::OK);
        ASSERT_EVENTS_ProductRetired(0, 16U, 1U);
        ASSERT_from_capacityOut(0, 1U, true);
        ASSERT_NE(::access(this->directory.path("product_16.bin").c_str(), F_OK), 0);
        ASSERT_NE(::access(this->directory.path("product_16.meta").c_str(), F_OK), 0);
        ASSERT_NE(::access(this->directory.path("product_16.retire").c_str(), F_OK), 0);
        this->submit(17U, TEST_CRC);
        ASSERT_from_productCommitOut(0, 17U, TEST_SIZE, true);
        ASSERT_EQ(this->readFile("product_1.bin"), TEST_BYTES);
    }

    void testRetirementRecoveryAndFailure() {
        ASSERT_TRUE(this->writeSource());
        std::ofstream csvSource(this->directory.path("source.csv"), std::ios::binary);
        csvSource.write(TEST_BYTES, TEST_SIZE);
        csvSource.close();
        ASSERT_TRUE(csvSource.good());
        this->invoke_to_provenanceIn(0, 17U, 1113U, 2U);
        this->component.doDispatch();
        this->invoke_to_requestIn(0, 17U, TEST_SIZE, ScienceProductSource::TEST,
            Fw::String(this->directory.path("source.csv").c_str()), TEST_CRC);
        this->component.doDispatch();
        this->submit(18U, TEST_CRC);
        const std::string labelledName = "daq_1113_run_2_product_17.csv";
        // A marker creation failure must not free a slot or announce retirement.
        ASSERT_EQ(::mkdir(this->directory.path("product_17.retire").c_str(), 0750), 0);
        this->clearHistory();
        this->sendCmd_RETIRE_PRODUCT(0, 0U, 17U, 1U);
        this->component.doDispatch();
        ASSERT_CMD_RESPONSE(0, StorageManagerComponentBase::OPCODE_RETIRE_PRODUCT, 0U,
                            Fw::CmdResponse::EXECUTION_ERROR);
        ASSERT_EVENTS_ProductRetirementFailed(0, 17U);
        ASSERT_EVENTS_ProductRetired_SIZE(0);
        ASSERT_from_capacityOut(0, 0U, false);
        ASSERT_EQ(this->readFile(labelledName.c_str()), TEST_BYTES);
        ASSERT_EQ(::rmdir(this->directory.path("product_17.retire").c_str()), 0);

        // Simulate a crash after explicit marker persistence and metadata unlink.
        std::ofstream marker(this->directory.path("product_17.retire"));
        marker << labelledName << "\n";
        marker.close();
        ASSERT_TRUE(marker.good());
        ASSERT_EQ(::unlink(this->directory.path("product_17.meta").c_str()), 0);
        this->clearHistory();
        auto destroy = [](StorageManager* value) { value->deinit(); delete value; };
        std::unique_ptr<StorageManager, decltype(destroy)> recovered(new StorageManager("Recovered"), destroy);
        this->initialize(*recovered);
        this->invoke_to_run(0, 0U);
        recovered->doDispatch();
        ASSERT_from_capacityOut(0, 15U, true);
        ASSERT_EVENTS_ScienceProductRecovered_SIZE(1);
        ASSERT_NE(::access(this->directory.path(labelledName.c_str()).c_str(), F_OK), 0);
        ASSERT_NE(::access(this->directory.path("product_17.retire").c_str(), F_OK), 0);
        ASSERT_EQ(this->readFile("product_18.bin"), TEST_BYTES);
        U32 reserved = 0U;
        ASSERT_TRUE(reserveProductId(reserved));
        ASSERT_GT(reserved, 18U);  // Retiring products never reuses their IDs.
    }

  private:
    std::string readFile(const char* name) const {
        std::ifstream input(this->directory.path(name), std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

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
    StorageManagerTester::CatalogTester tester;
    tester.testCommitAndDeliverExactProduct();
}

TEST(StorageManagerCommit, RejectBadCrc) {
    StorageManagerTester::CatalogTester tester;
    tester.testRejectBadCrc();
}

TEST(StorageManagerCommit, ProvenanceNamesAndRecovery) {
    StorageManagerTester::CatalogTester tester;
    tester.testProvenanceNamesAndRecovery();
}

TEST(StorageManagerCommit, RejectMismatchedRecoveredProvenance) {
    StorageManagerTester::CatalogTester tester;
    tester.testRejectMismatchedRecoveredProvenance();
}

TEST(StorageManagerCommit, ConflictingAndConsumedProvenance) {
    StorageManagerTester::CatalogTester tester;
    tester.testConflictingAndConsumedProvenance();
}

TEST(StorageManagerCommit, ProvenanceCapacityFailsClosed) {
    StorageManagerTester::CatalogTester tester;
    tester.testProvenanceCapacityFailsClosed();
}

TEST(StorageManagerCommit, OrphanProvenanceReservesId) {
    StorageManagerTester::CatalogTester tester;
    tester.testOrphanProvenanceReservesId();
}

TEST(StorageManagerCommit, CapacityAndExplicitRetirement) {
    StorageManagerTester::CatalogTester tester;
    tester.testCapacityAndExplicitRetirement();
}

TEST(StorageManagerCommit, RetirementRecoveryAndFailure) {
    StorageManagerTester::CatalogTester tester;
    tester.testRetirementRecoveryAndFailure();
}

TEST(ProductIdFilename, LegacyAndAcquisitionNames) {
    for (const char* name : {"product_17.bin", "product_17.csv", "product_17.meta",
                             "daq_1113_run_0_product_17.bin", "daq_1113_run_0_product_17.csv"}) {
        U32 parsed = 0U;
        ASSERT_TRUE(parseProductIdFilename(name, parsed)) << name;
        ASSERT_EQ(parsed, 17U);
    }
    U32 parsed = 0U;
    ASSERT_TRUE(parseProductIdFilename("daq_4294967295_run_4294967295_product_4294967295.csv", parsed));
    ASSERT_EQ(parsed, 0xFFFFFFFFU);
    for (const char* name : {"product_0.bin", "product_4294967296.bin", "product_17.csv.tmp",
                             "daq_1113_run_0_product_17.meta", "daq__run_0_product_17.csv",
                             "daq_1113_run_-1_product_17.csv", "daq_4294967296_run_0_product_17.csv",
                             "daq_1113_run_0_product_17.csv.extra"}) {
        ASSERT_FALSE(parseProductIdFilename(name, parsed)) << name;
    }
}

}  // namespace Components
