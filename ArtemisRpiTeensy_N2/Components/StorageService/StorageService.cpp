#include "Components/StorageService/StorageService.hpp"

#include <cstring>
#include <dirent.h>
#include <string>
#include <unistd.h>

namespace Components {

namespace {

constexpr const char* CAPTURE_DIR = "/tmp/neutron_payload_captures";
constexpr const char* CAPTURE_PREFIX = "neutron_capture_";
constexpr const char* CAPTURE_SUFFIX = ".csv";

bool hasPrefix(const char* value, const char* prefix) {
    return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}

bool hasSuffix(const char* value, const char* suffix) {
    const std::size_t valueLength = std::strlen(value);
    const std::size_t suffixLength = std::strlen(suffix);
    if (valueLength < suffixLength) {
        return false;
    }
    return std::strcmp(value + valueLength - suffixLength, suffix) == 0;
}

}  // namespace

StorageService::StorageService(const char* const compName)
    : StorageServiceComponentBase(compName),
      m_storedProducts(0),
      m_lastProductSize(0),
      m_historyNext(0),
      m_historyCount(0),
      m_removedDatasetFiles(0),
      m_removeDatasetFailures(0),
      m_history{} {}

StorageService::~StorageService() {}

void StorageService::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void StorageService::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->tlmWrite_StoredProducts(this->m_storedProducts);
    this->tlmWrite_LastProductSize(this->m_lastProductSize);
    this->tlmWrite_StorageHistoryDepth(this->historyDepth());
    this->tlmWrite_RemovedDatasetFiles(this->m_removedDatasetFiles);
    this->tlmWrite_RemoveDatasetFailures(this->m_removeDatasetFailures);
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        const Components::HealthState health =
            (this->m_removeDatasetFailures == 0U) ? Components::HealthState::OK : Components::HealthState::WARN;
        this->sohStatusOut_out(0, health, this->m_storedProducts);
    }
}

void StorageService::requestIn_handler(FwIndexType portNum, U32 productBytes) {
    static_cast<void>(portNum);
    this->m_storedProducts += 1;
    this->m_lastProductSize = productBytes;
    this->rememberProduct(this->m_storedProducts, this->m_lastProductSize);
    this->log_ACTIVITY_HI_ScienceStored(this->m_storedProducts, this->m_lastProductSize);
    if (this->isConnected_downlinkReadyOut_OutputPort(0)) {
        this->downlinkReadyOut_out(0, this->m_lastProductSize);
    }
}

void StorageService::downlinkRequestIn_handler(FwIndexType portNum, U32 productBytes) {
    static_cast<void>(portNum);
    static_cast<void>(productBytes);
    this->log_ACTIVITY_LO_DownlinkPrepared(this->m_lastProductSize);
    if (this->isConnected_downlinkReadyOut_OutputPort(0)) {
        this->downlinkReadyOut_out(0, this->m_lastProductSize);
    }
}

void StorageService::REPORT_STORAGE_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->reportLatest();
    this->log_ACTIVITY_LO_DownlinkPrepared(this->m_lastProductSize);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageService::REPORT_LATEST_DATASET_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->reportLatest();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageService::REPORT_STORAGE_HISTORY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->reportHistory();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageService::REMOVE_OLD_DATASETS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 confirm) {
    if (confirm != REMOVE_CONFIRM) {
        this->log_WARNING_LO_RemoveOldDatasetsRejected(confirm);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
        return;
    }

    U32 removedFiles = 0;
    U32 failedFiles = 0;
    this->removeCaptureFiles(removedFiles, failedFiles);
    this->m_removedDatasetFiles = removedFiles;
    this->m_removeDatasetFailures = failedFiles;
    this->m_storedProducts = 0;
    this->m_lastProductSize = 0;
    this->clearHistory();

    this->tlmWrite_StoredProducts(this->m_storedProducts);
    this->tlmWrite_LastProductSize(this->m_lastProductSize);
    this->tlmWrite_StorageHistoryDepth(this->historyDepth());
    this->tlmWrite_RemovedDatasetFiles(this->m_removedDatasetFiles);
    this->tlmWrite_RemoveDatasetFailures(this->m_removeDatasetFailures);
    this->log_ACTIVITY_HI_OldDatasetsRemoved(removedFiles, failedFiles);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageService::rememberProduct(U32 productCount, U32 productBytes) {
    this->m_history[this->m_historyNext] = ProductRecord{productCount, productBytes};
    this->m_historyNext = (this->m_historyNext + 1U) % HISTORY_CAPACITY;
    if (this->m_historyCount < HISTORY_CAPACITY) {
        this->m_historyCount += 1;
    }
}

void StorageService::reportLatest() const {
    if (this->m_storedProducts == 0U) {
        this->log_ACTIVITY_LO_StorageHistoryEmpty();
        return;
    }
    this->log_ACTIVITY_HI_LatestDataset(this->m_storedProducts, this->m_lastProductSize);
}

void StorageService::reportHistory() const {
    const U32 depth = this->historyDepth();
    if (depth == 0U) {
        this->log_ACTIVITY_LO_StorageHistoryEmpty();
        return;
    }

    const U32 oldest = (this->m_historyCount == HISTORY_CAPACITY) ? this->m_historyNext : 0U;
    for (U32 slot = 0; slot < depth; ++slot) {
        const U32 index = (oldest + slot) % HISTORY_CAPACITY;
        const ProductRecord& record = this->m_history[index];
        this->log_ACTIVITY_LO_StorageHistoryEntry(slot, record.productCount, record.productBytes);
    }
}

U32 StorageService::historyDepth() const {
    return this->m_historyCount;
}

void StorageService::clearHistory() {
    this->m_historyNext = 0;
    this->m_historyCount = 0;
    for (U32 index = 0; index < HISTORY_CAPACITY; ++index) {
        this->m_history[index] = ProductRecord{0, 0};
    }
}

void StorageService::removeCaptureFiles(U32& removedFiles, U32& failedFiles) const {
    removedFiles = 0;
    failedFiles = 0;

    DIR* directory = ::opendir(CAPTURE_DIR);
    if (directory == nullptr) {
        return;
    }

    while (dirent* entry = ::readdir(directory)) {
        const char* name = entry->d_name;
        if (!hasPrefix(name, CAPTURE_PREFIX) || !hasSuffix(name, CAPTURE_SUFFIX)) {
            continue;
        }

        const std::string path = std::string(CAPTURE_DIR) + "/" + name;
        if (::unlink(path.c_str()) == 0) {
            removedFiles += 1;
        } else {
            failedFiles += 1;
        }
    }
    (void)::closedir(directory);
}

}  // namespace Components
