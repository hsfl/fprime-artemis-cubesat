#include "Components/StorageManager/StorageManager.hpp"
#include "Components/LinkCfg/PayloadPaths.hpp"

#include <cstring>
#include <dirent.h>
#include <string>
#include <unistd.h>

namespace Components {

namespace {

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

StorageManager::StorageManager(const char* const compName)
    : StorageManagerComponentBase(compName),
      m_storedProducts(0),
      m_lastProductSize(0),
      m_historyNext(0),
      m_historyCount(0),
      m_removedDatasetFiles(0),
      m_removeDatasetFailures(0),
      m_lastProductId(0),
      m_lastSourceKind(Components::ScienceProductSource::UNKNOWN),
      m_lastSourcePath(""),
      m_lastSourceCrc(0),
      m_history{} {}

StorageManager::~StorageManager() {}

void StorageManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void StorageManager::run_handler(FwIndexType portNum, U32 context) {
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

void StorageManager::requestIn_handler(FwIndexType portNum,
                                       U32 productId,
                                       U32 productBytes,
                                       const Components::ScienceProductSource& sourceKind,
                                       const Fw::StringBase& sourcePath,
                                       U32 sourceCrc) {
    static_cast<void>(portNum);
    this->m_storedProducts += 1;
    this->m_lastProductId = productId;
    this->m_lastProductSize = productBytes;
    this->m_lastSourceKind = sourceKind;
    this->m_lastSourcePath = sourcePath;
    this->m_lastSourceCrc = sourceCrc;
    this->rememberProduct(this->m_storedProducts, this->m_lastProductId, this->m_lastProductSize);
    this->log_ACTIVITY_HI_ScienceStored(this->m_storedProducts, this->m_lastProductSize);
    if (this->isConnected_downlinkReadyOut_OutputPort(0)) {
        this->downlinkReadyOut_out(0,
                                   this->m_lastProductId,
                                   this->m_lastProductSize,
                                   this->m_lastSourceKind,
                                   this->m_lastSourcePath,
                                   this->m_lastSourceCrc);
    }
}

void StorageManager::downlinkRequestIn_handler(FwIndexType portNum,
                                               U32 productId,
                                               U32 productBytes,
                                               const Components::ScienceProductSource& sourceKind,
                                               const Fw::StringBase& sourcePath,
                                               U32 sourceCrc) {
    static_cast<void>(portNum);
    this->m_lastProductId = productId;
    this->m_lastProductSize = productBytes;
    this->m_lastSourceKind = sourceKind;
    this->m_lastSourcePath = sourcePath;
    this->m_lastSourceCrc = sourceCrc;
    this->log_ACTIVITY_LO_DownlinkPrepared(this->m_lastProductSize);
    if (this->isConnected_downlinkReadyOut_OutputPort(0)) {
        this->downlinkReadyOut_out(0,
                                   this->m_lastProductId,
                                   this->m_lastProductSize,
                                   this->m_lastSourceKind,
                                   this->m_lastSourcePath,
                                   this->m_lastSourceCrc);
    }
}

void StorageManager::REPORT_STORAGE_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->reportLatest();
    this->log_ACTIVITY_LO_DownlinkPrepared(this->m_lastProductSize);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageManager::REPORT_LATEST_DATASET_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->reportLatest();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageManager::REPORT_STORAGE_HISTORY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->reportHistory();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageManager::REMOVE_OLD_DATASETS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 confirm) {
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
    this->m_lastProductId = 0;
    this->m_lastSourceKind = Components::ScienceProductSource::UNKNOWN;
    this->m_lastSourcePath = "";
    this->m_lastSourceCrc = 0;
    this->clearHistory();

    this->tlmWrite_StoredProducts(this->m_storedProducts);
    this->tlmWrite_LastProductSize(this->m_lastProductSize);
    this->tlmWrite_StorageHistoryDepth(this->historyDepth());
    this->tlmWrite_RemovedDatasetFiles(this->m_removedDatasetFiles);
    this->tlmWrite_RemoveDatasetFailures(this->m_removeDatasetFailures);
    this->log_ACTIVITY_HI_OldDatasetsRemoved(removedFiles, failedFiles);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageManager::rememberProduct(U32 productCount, U32 productId, U32 productBytes) {
    this->m_history[this->m_historyNext] = ProductRecord{productCount, productId, productBytes};
    this->m_historyNext = (this->m_historyNext + 1U) % HISTORY_CAPACITY;
    if (this->m_historyCount < HISTORY_CAPACITY) {
        this->m_historyCount += 1;
    }
}

void StorageManager::reportLatest() const {
    if (this->m_storedProducts == 0U) {
        this->log_ACTIVITY_LO_StorageHistoryEmpty();
        return;
    }
    this->log_ACTIVITY_HI_LatestDataset(this->m_storedProducts, this->m_lastProductSize);
}

void StorageManager::reportHistory() const {
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

U32 StorageManager::historyDepth() const {
    return this->m_historyCount;
}

void StorageManager::clearHistory() {
    this->m_historyNext = 0;
    this->m_historyCount = 0;
    for (U32 index = 0; index < HISTORY_CAPACITY; ++index) {
        this->m_history[index] = ProductRecord{0, 0, 0};
    }
}

void StorageManager::removeCaptureFiles(U32& removedFiles, U32& failedFiles) const {
    removedFiles = 0;
    failedFiles = 0;

    const std::string captureDir = Components::LinkCfg::payloadCaptureDir();
    DIR* directory = ::opendir(captureDir.c_str());
    if (directory == nullptr) {
        return;
    }

    while (dirent* entry = ::readdir(directory)) {
        const char* name = entry->d_name;
        if (!hasPrefix(name, CAPTURE_PREFIX) || !hasSuffix(name, CAPTURE_SUFFIX)) {
            continue;
        }

        const std::string path = captureDir + "/" + name;
        if (::unlink(path.c_str()) == 0) {
            removedFiles += 1;
        } else {
            failedFiles += 1;
        }
    }
    (void)::closedir(directory);
}

}  // namespace Components
