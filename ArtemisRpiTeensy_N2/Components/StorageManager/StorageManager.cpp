#include "Components/StorageManager/StorageManager.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Components {

namespace {

constexpr const char* DEFAULT_STORAGE_ROOT = "/home/pi/artemis/data/science-products";
constexpr const char* ID_HIGH_WATER_FILE = "product-id-high-water";
constexpr const char* PRODUCT_PREFIX = "product_";
constexpr const char* PRODUCT_DATA_SUFFIX = ".bin";
constexpr const char* PRODUCT_METADATA_SUFFIX = ".meta";
constexpr const char* TRANSIENT_CAPTURE_DIR = "/tmp/neutron_payload_captures";
constexpr const char* TRANSIENT_CAPTURE_PREFIX = "neutron_capture_";
constexpr const char* TRANSIENT_CAPTURE_SUFFIX = ".csv";
constexpr U32 SOURCE_PATH_CAPACITY = 192U;
constexpr U32 REJECT_INVALID_DESCRIPTOR = 1U;
constexpr U32 REJECT_CATALOG_FULL = 2U;
constexpr U32 REJECT_COPY_OR_CRC = 3U;
constexpr U32 REJECT_DUPLICATE_ID = 4U;
constexpr U32 REJECT_METADATA_COMMIT = 5U;
constexpr U32 REJECT_INVALID_STORED_FILE = 6U;
constexpr U32 REJECT_STORAGE_UNAVAILABLE = 7U;
constexpr U32 REJECT_ID_COUNTER = 8U;
constexpr U32 REJECT_PATH_TOO_LONG = 9U;
constexpr U32 STORAGE_UNAVAILABLE_ROOT = 1U;
constexpr U32 STORAGE_UNAVAILABLE_CATALOG_OVERFLOW = 2U;
constexpr U32 STORAGE_UNAVAILABLE_ID_COUNTER = 3U;

std::string getStorageRoot() {
    const char* configured = std::getenv("N2_STORAGE_ROOT");
    if ((configured != nullptr) && (configured[0] != '\0')) {
        return std::string(configured);
    }
    return std::string(DEFAULT_STORAGE_ROOT);
}

std::string productDataPath(const std::string& root, U32 productId) {
    return root + "/" + PRODUCT_PREFIX + std::to_string(productId) + PRODUCT_DATA_SUFFIX;
}

std::string productMetadataPath(const std::string& root, U32 productId) {
    return root + "/" + PRODUCT_PREFIX + std::to_string(productId) + PRODUCT_METADATA_SUFFIX;
}

bool hasPrefix(const char* value, const char* prefix) {
    return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}

bool hasSuffix(const char* value, const char* suffix) {
    const std::size_t valueLength = std::strlen(value);
    const std::size_t suffixLength = std::strlen(suffix);
    return (valueLength >= suffixLength) &&
           (std::strcmp(value + valueLength - suffixLength, suffix) == 0);
}

bool ensureDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::string current;
    std::size_t position = 0U;
    if (path[0] == '/') {
        current = "/";
        position = 1U;
    }
    while (position < path.size()) {
        const std::size_t next = path.find('/', position);
        const std::string part = path.substr(position,
                                             (next == std::string::npos) ? std::string::npos : next - position);
        position = (next == std::string::npos) ? path.size() : next + 1U;
        if (part.empty() || (part == ".")) {
            continue;
        }
        if (!current.empty() && (current.back() != '/')) {
            current += "/";
        }
        current += part;
        const std::string parent = (current.find_last_of('/') == std::string::npos)
            ? std::string(".")
            : ((current.find_last_of('/') == 0U) ? std::string("/") : current.substr(0U, current.find_last_of('/')));
        const bool created = (::mkdir(current.c_str(), 0750) == 0);
        if (!created && (errno != EEXIST)) {
            return false;
        }
        struct stat info {};
        if ((::stat(current.c_str(), &info) != 0) || !S_ISDIR(info.st_mode)) {
            return false;
        }
        if (created) {
            const int parentFd = ::open(parent.c_str(), O_RDONLY);
            if ((parentFd < 0) || (::fsync(parentFd) != 0)) {
                if (parentFd >= 0) {
                    (void)::close(parentFd);
                }
                return false;
            }
            (void)::close(parentFd);
        }
    }
    return true;
}

bool parseProductFileId(const char* name, U32& productId) {
    const std::size_t prefixLength = std::strlen(PRODUCT_PREFIX);
    if ((std::strncmp(name, PRODUCT_PREFIX, prefixLength) != 0) ||
        (!hasSuffix(name, PRODUCT_DATA_SUFFIX) && !hasSuffix(name, PRODUCT_METADATA_SUFFIX))) {
        return false;
    }
    const std::size_t nameLength = std::strlen(name);
    const std::size_t suffixLength = hasSuffix(name, PRODUCT_DATA_SUFFIX)
        ? std::strlen(PRODUCT_DATA_SUFFIX)
        : std::strlen(PRODUCT_METADATA_SUFFIX);
    if (nameLength <= prefixLength + suffixLength) {
        return false;
    }
    const char* firstDigit = name + prefixLength;
    const char* endDigits = name + nameLength - suffixLength;
    U64 value = 0U;
    for (const char* digit = firstDigit; digit < endDigits; ++digit) {
        if ((*digit < '0') || (*digit > '9')) {
            return false;
        }
        value = (value * 10U) + static_cast<U64>(*digit - '0');
        if (value > std::numeric_limits<U32>::max()) {
            return false;
        }
    }
    if (value == 0U) {
        return false;
    }
    productId = static_cast<U32>(value);
    return true;
}

bool parseHighWaterLine(const char* line, U32& nextProductId) {
    constexpr const char* PREFIX = "N2ID1 ";
    if (std::strncmp(line, PREFIX, std::strlen(PREFIX)) != 0) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(line + std::strlen(PREFIX), &end, 10);
    if ((errno != 0) || (end == line + std::strlen(PREFIX)) ||
        (value > std::numeric_limits<U32>::max())) {
        return false;
    }
    while ((*end == ' ') || (*end == '\t') || (*end == '\r') || (*end == '\n')) {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    nextProductId = static_cast<U32>(value);  // Zero records that U32 IDs are exhausted.
    return true;
}

bool readHighWaterFile(const std::string& path, bool& exists, U32& nextProductId) {
    exists = false;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return errno == ENOENT;
    }
    exists = true;
    char line[64] = {};
    const bool readOk = (std::fgets(line, sizeof(line), file) != nullptr) &&
                        (std::fgetc(file) == EOF) && !std::ferror(file);
    (void)std::fclose(file);
    return readOk && parseHighWaterLine(line, nextProductId);
}

class IdCounterLock {
  public:
    explicit IdCounterLock(const std::string& storageRoot)
        : m_fd(::open((storageRoot + "/product-id.lock").c_str(), O_RDWR | O_CREAT, 0640)) {
        if (m_fd >= 0) {
            int result = 0;
            do {
                result = ::flock(m_fd, LOCK_EX);
            } while ((result != 0) && (errno == EINTR));
            if (result != 0) {
                (void)::close(m_fd);
                m_fd = -1;
            }
        }
    }

    ~IdCounterLock() {
        if (m_fd >= 0) {
            (void)::flock(m_fd, LOCK_UN);
            (void)::close(m_fd);
        }
    }

    bool locked() const { return m_fd >= 0; }

  private:
    int m_fd;
};

bool writeAll(int fd, const void* buffer, std::size_t size);

bool writeHighWaterUnlocked(const std::string& storageRoot, U32 nextProductId) {
    const std::string markerPath = storageRoot + "/" + ID_HIGH_WATER_FILE;
    char contents[64];
    const int length = std::snprintf(contents, sizeof(contents), "N2ID1 %u\n", nextProductId);
    if ((length <= 0) || (static_cast<std::size_t>(length) >= sizeof(contents))) {
        return false;
    }
    std::string temporaryPath;
    int fd = -1;
    for (U32 attempt = 0U; attempt < 8U; ++attempt) {
        temporaryPath = markerPath + ".tmp." + std::to_string(::getpid()) + "." + std::to_string(attempt);
        fd = ::open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0640);
        if (fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            return false;
        }
    }
    if (fd < 0) {
        return false;
    }
    bool ok = writeAll(fd, contents, static_cast<std::size_t>(length)) && (::fsync(fd) == 0);
    if (::close(fd) != 0) {
        ok = false;
    }
    if (!ok || (::rename(temporaryPath.c_str(), markerPath.c_str()) != 0)) {
        (void)::unlink(temporaryPath.c_str());
        return false;
    }
    const int directoryFd = ::open(storageRoot.c_str(), O_RDONLY);
    if (directoryFd < 0) {
        return false;
    }
    const bool directorySynced = (::fsync(directoryFd) == 0);
    (void)::close(directoryFd);
    return directorySynced;
}

bool writeAll(int fd, const void* buffer, std::size_t size) {
    const char* bytes = static_cast<const char*>(buffer);
    std::size_t written = 0U;
    while (written < size) {
        const ssize_t count = ::write(fd, bytes + written, size - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    return true;
}

void updateCrc16(U16& crc, const U8* bytes, std::size_t size) {
    for (std::size_t index = 0U; index < size; ++index) {
        crc ^= static_cast<U16>(bytes[index]) << 8U;
        for (U8 bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 0x8000U) != 0U)
                ? static_cast<U16>((crc << 1U) ^ 0x1021U)
                : static_cast<U16>(crc << 1U);
        }
    }
}

bool crcFile(const std::string& path, U32& sizeOut, U32& crcOut) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    struct stat info {};
    if ((::fstat(fd, &info) != 0) || !S_ISREG(info.st_mode) ||
        (info.st_size <= 0) || (static_cast<U64>(info.st_size) > std::numeric_limits<U32>::max())) {
        (void)::close(fd);
        return false;
    }
    U16 crc = 0xFFFFU;
    U32 size = 0U;
    U8 buffer[4096];
    for (;;) {
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void)::close(fd);
            return false;
        }
        if (count == 0) {
            break;
        }
        if (static_cast<U64>(size) + static_cast<U64>(count) > std::numeric_limits<U32>::max()) {
            (void)::close(fd);
            return false;
        }
        updateCrc16(crc, buffer, static_cast<std::size_t>(count));
        size += static_cast<U32>(count);
    }
    const bool readOk = (::close(fd) == 0);
    sizeOut = size;
    crcOut = static_cast<U32>(crc);
    return readOk && (size == static_cast<U32>(info.st_size));
}

}  // namespace

StorageManager::StorageManager(const char* const compName)
    : StorageManagerComponentBase(compName),
      m_storedProducts(0U),
      m_lastProductSize(0U),
      m_selectedProductId(0U),
      m_lastInputProductId(0U),
      m_nextProductId(1U),
      m_removedDatasetFiles(0U),
      m_removeDatasetFailures(0U),
      m_catalogOverflow(0U),
      m_catalogLoaded(false),
      m_storageReady(false),
      m_idCounterExhausted(false),
      m_recoveryReported(false),
      m_storageRoot(getStorageRoot()),
      m_history{} {}

StorageManager::~StorageManager() {}

void StorageManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void StorageManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    static_cast<void>(this->ensureCatalogLoaded());
    this->tlmWrite_StoredProducts(this->m_storedProducts);
    this->tlmWrite_LastProductSize(this->m_lastProductSize);
    this->tlmWrite_StorageHistoryDepth(this->historyDepth());
    this->tlmWrite_RemovedDatasetFiles(this->m_removedDatasetFiles);
    this->tlmWrite_RemoveDatasetFailures(this->m_removeDatasetFailures);
    this->tlmWrite_SelectedProductId(this->m_selectedProductId);
    this->tlmWrite_StorageReady(this->m_storageReady);
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        const Components::HealthState health = this->m_storageReady
            ? ((this->m_removeDatasetFailures == 0U) ? Components::HealthState::OK : Components::HealthState::WARN)
            : Components::HealthState::FAIL;
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
    const bool stored = this->storeProduct(productId, productBytes, sourceKind, sourcePath, sourceCrc);
    if (this->isConnected_productStoredOut_OutputPort(0)) {
        this->productStoredOut_out(0, productId, stored);
    }
}

bool StorageManager::storeProduct(U32 productId,
                                  U32 productBytes,
                                  const Components::ScienceProductSource& sourceKind,
                                  const Fw::StringBase& sourcePath,
                                  U32 sourceCrc) {
    this->m_lastInputProductId = 0U;
    if (!this->ensureCatalogLoaded()) {
        this->rejectProduct(productId, REJECT_STORAGE_UNAVAILABLE, sourcePath);
        return false;
    }
    if ((productId == 0U) || (productBytes == 0U) ||
        (sourceKind == Components::ScienceProductSource::UNKNOWN) || (sourcePath.length() == 0U) ||
        (sourceCrc > 0xFFFFU)) {
        this->rejectProduct(productId, REJECT_INVALID_DESCRIPTOR, sourcePath);
        return false;
    }
    if (sourcePath.length() >= SOURCE_PATH_CAPACITY) {
        this->rejectProduct(productId, REJECT_PATH_TOO_LONG, sourcePath);
        return false;
    }
    if (this->m_storedProducts >= HISTORY_CAPACITY) {
        this->rejectProduct(productId, REJECT_CATALOG_FULL, sourcePath);
        return false;
    }
    if ((this->findProduct(productId) != nullptr) ||
        (::access(productDataPath(this->m_storageRoot, productId).c_str(), F_OK) == 0) ||
        (::access(productMetadataPath(this->m_storageRoot, productId).c_str(), F_OK) == 0)) {
        this->rejectProduct(productId, REJECT_DUPLICATE_ID, sourcePath);
        return false;
    }

    const std::string dataPath = productDataPath(this->m_storageRoot, productId);
    const std::string metadataPath = productMetadataPath(this->m_storageRoot, productId);
    if (dataPath.size() >= SOURCE_PATH_CAPACITY) {
        this->rejectProduct(productId, REJECT_PATH_TOO_LONG, sourcePath);
        return false;
    }
    if (!this->persistNextProductId((productId == std::numeric_limits<U32>::max()) ? 0U : productId + 1U)) {
        this->rejectProduct(productId, REJECT_ID_COUNTER, sourcePath);
        return false;
    }
    if (!this->copyProductAtomically(sourcePath, dataPath, productBytes, sourceCrc)) {
        this->rejectProduct(productId, REJECT_COPY_OR_CRC, sourcePath);
        return false;
    }
    if (!this->writeMetadataAtomically(metadataPath, productId, productBytes, sourceKind, sourceCrc)) {
        this->rejectProduct(productId, REJECT_METADATA_COMMIT, Fw::String(dataPath.c_str()));
        return false;
    }

    const Fw::String storedPath(dataPath.c_str());
    ProductRecord& record = this->m_history[this->m_storedProducts];
    record = ProductRecord{productId, productId, productBytes, sourceCrc, sourceKind, storedPath};
    this->m_storedProducts += 1U;
    this->m_lastInputProductId = productId;
    this->m_lastProductSize = productBytes;
    this->m_selectedProductId = productId;
    this->log_ACTIVITY_HI_ScienceStored(this->m_storedProducts, productBytes);
    this->log_ACTIVITY_HI_ScienceProductCommitted(productId, productBytes, sourceCrc, storedPath);
    this->publishSelected(record, false);
    this->tlmWrite_StoredProducts(this->m_storedProducts);
    this->tlmWrite_LastProductSize(this->m_lastProductSize);
    this->tlmWrite_StorageHistoryDepth(this->historyDepth());
    this->tlmWrite_SelectedProductId(this->m_selectedProductId);
    this->tlmWrite_StorageReady(true);
    return true;
}

void StorageManager::deliveryRequestIn_handler(FwIndexType portNum, U32 productId) {
    static_cast<void>(portNum);
    if (!this->ensureCatalogLoaded()) {
        this->rejectProduct(productId, REJECT_STORAGE_UNAVAILABLE, Fw::String(""));
        return;
    }
    if ((productId == 0U) || (productId != this->m_lastInputProductId) ||
        (productId != this->m_selectedProductId)) {
        this->rejectProduct(productId, REJECT_INVALID_DESCRIPTOR, Fw::String(""));
        return;
    }
    ProductRecord* record = this->findProduct(this->m_selectedProductId);
    if ((record == nullptr) || !this->isValidStoredProduct(*record)) {
        this->rejectProduct(productId, REJECT_INVALID_STORED_FILE,
                            (record == nullptr) ? Fw::String("") : record->sourcePath);
        return;
    }
    if (this->isConnected_deliveryDescriptorOut_OutputPort(0)) {
        this->log_ACTIVITY_LO_DownlinkPrepared(record->productBytes);
        this->deliveryDescriptorOut_out(0,
                                        record->productId,
                                        record->productBytes,
                                        record->sourceKind,
                                        record->sourcePath,
                                        record->sourceCrc);
    }
}

void StorageManager::downlinkRequestIn_handler(FwIndexType portNum,
                                               U32 productId,
                                               U32 productBytes,
                                               const Components::ScienceProductSource& sourceKind,
                                               const Fw::StringBase& sourcePath,
                                               U32 sourceCrc) {
    static_cast<void>(portNum);
    if (!this->ensureCatalogLoaded()) {
        this->rejectProduct(productId, REJECT_STORAGE_UNAVAILABLE, sourcePath);
        return;
    }
    const ProductRecord* record = this->findProduct(productId);
    const bool matchesSelected = (record != nullptr) && (productId == this->m_selectedProductId) &&
                                 (productBytes == record->productBytes) && (sourceKind == record->sourceKind) &&
                                 (sourceCrc == record->sourceCrc) &&
                                 (std::strcmp(sourcePath.toChar(), record->sourcePath.toChar()) == 0);
    if (!matchesSelected || !this->isValidStoredProduct(*record)) {
        this->rejectProduct(productId, REJECT_INVALID_STORED_FILE, sourcePath);
        return;
    }
    if (this->isConnected_downlinkReadyOut_OutputPort(0)) {
        this->log_ACTIVITY_LO_DownlinkPrepared(record->productBytes);
        this->downlinkReadyOut_out(0,
                                   record->productId,
                                   record->productBytes,
                                   record->sourceKind,
                                   record->sourcePath,
                                   record->sourceCrc);
    }
}

void StorageManager::REPORT_STORAGE_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (!this->ensureCatalogLoaded()) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->reportLatest();
    const ProductRecord* selected = this->findProduct(this->m_selectedProductId);
    if (selected != nullptr) {
        this->log_ACTIVITY_LO_DownlinkPrepared(selected->productBytes);
    }
    this->tlmWrite_StoredProducts(this->m_storedProducts);
    this->tlmWrite_LastProductSize(this->m_lastProductSize);
    this->tlmWrite_StorageHistoryDepth(this->historyDepth());
    this->tlmWrite_SelectedProductId(this->m_selectedProductId);
    this->tlmWrite_StorageReady(true);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageManager::REPORT_LATEST_DATASET_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (!this->ensureCatalogLoaded()) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->reportLatest();
    this->tlmWrite_StoredProducts(this->m_storedProducts);
    this->tlmWrite_LastProductSize(this->m_lastProductSize);
    this->tlmWrite_StorageHistoryDepth(this->historyDepth());
    this->tlmWrite_SelectedProductId(this->m_selectedProductId);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageManager::REPORT_STORAGE_HISTORY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (!this->ensureCatalogLoaded()) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->reportHistory();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageManager::REMOVE_OLD_DATASETS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 confirm) {
    if (confirm != REMOVE_CONFIRM) {
        this->log_WARNING_LO_RemoveOldDatasetsRejected(confirm);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
        return;
    }

    U32 removedFiles = 0U;
    U32 failedFiles = 0U;
    this->removeCaptureFiles(removedFiles, failedFiles);
    this->m_removedDatasetFiles = removedFiles;
    this->m_removeDatasetFailures = failedFiles;
    this->tlmWrite_StoredProducts(this->m_storedProducts);
    this->tlmWrite_LastProductSize(this->m_lastProductSize);
    this->tlmWrite_StorageHistoryDepth(this->historyDepth());
    this->tlmWrite_RemovedDatasetFiles(this->m_removedDatasetFiles);
    this->tlmWrite_RemoveDatasetFailures(this->m_removeDatasetFailures);
    this->log_ACTIVITY_HI_OldDatasetsRemoved(removedFiles, failedFiles);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StorageManager::SELECT_PRODUCT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 productId) {
    if (!this->ensureCatalogLoaded()) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    ProductRecord* record = this->findProduct(productId);
    if ((record == nullptr) || !this->isValidStoredProduct(*record)) {
        const Fw::String path = (record == nullptr) ? Fw::String("") : record->sourcePath;
        this->rejectProduct(productId, REJECT_INVALID_STORED_FILE, path);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->m_selectedProductId = productId;
    this->publishSelected(*record, true);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

bool StorageManager::ensureCatalogLoaded() {
    if (this->m_catalogLoaded) {
        return this->m_storageReady;
    }
    if (!this->loadCatalog()) {
        this->m_storageReady = false;
        const U32 reason = (this->m_catalogOverflow != 0U)
            ? STORAGE_UNAVAILABLE_CATALOG_OVERFLOW
            : ((this->m_idCounterExhausted) ? STORAGE_UNAVAILABLE_ID_COUNTER : STORAGE_UNAVAILABLE_ROOT);
        if (!this->m_recoveryReported) {
            this->log_WARNING_HI_StorageUnavailable(reason);
            this->m_recoveryReported = true;
        }
        this->tlmWrite_StorageReady(false);
        return false;
    }
    this->m_catalogLoaded = true;
    this->m_storageReady = true;
    this->m_recoveryReported = true;
    this->tlmWrite_StorageReady(true);
    for (U32 index = 0U; index < this->m_storedProducts; ++index) {
        const ProductRecord& record = this->m_history[index];
        this->log_ACTIVITY_HI_ScienceProductRecovered(record.productId, record.productBytes,
                                                       record.sourceCrc, record.sourcePath);
    }
    this->m_selectedProductId = (this->m_storedProducts > 0U)
        ? this->m_history[this->m_storedProducts - 1U].productId
        : 0U;
    if (this->m_selectedProductId != 0U) {
        this->m_lastProductSize = this->m_history[this->m_storedProducts - 1U].productBytes;
    }
    this->tlmWrite_SelectedProductId(this->m_selectedProductId);
    this->tlmWrite_StoredProducts(this->m_storedProducts);
    this->tlmWrite_LastProductSize(this->m_lastProductSize);
    this->tlmWrite_StorageHistoryDepth(this->historyDepth());
    return true;
}

bool StorageManager::loadCatalog() {
    this->m_storedProducts = 0U;
    this->m_lastProductSize = 0U;
    this->m_selectedProductId = 0U;
    this->m_lastInputProductId = 0U;
    this->m_catalogOverflow = 0U;
    if (!ensureDirectory(this->m_storageRoot)) {
        return false;
    }

    U32 highestReservedId = 0U;
    {
        IdCounterLock counterLock(this->m_storageRoot);
        if (!counterLock.locked()) {
            this->m_idCounterExhausted = true;
            return false;
        }
        const std::string highWaterPath = this->m_storageRoot + "/" + ID_HIGH_WATER_FILE;
        bool highWaterExists = false;
        U32 highWaterNext = 1U;
        if (!readHighWaterFile(highWaterPath, highWaterExists, highWaterNext)) {
            this->m_idCounterExhausted = true;
            return false;
        }
        DIR* directory = ::opendir(this->m_storageRoot.c_str());
        if (directory == nullptr) {
            return false;
        }
        struct dirent* entry = nullptr;
        bool scanOk = true;
        for (;;) {
            errno = 0;
            entry = ::readdir(directory);
            if (entry == nullptr) {
                scanOk = (errno == 0);
                break;
            }
            U32 parsedId = 0U;
            if (parseProductFileId(entry->d_name, parsedId) && (parsedId > highestReservedId)) {
                highestReservedId = parsedId;
            }
        }
        (void)::closedir(directory);
        if (!scanOk) {
            return false;
        }

        const bool idSpaceExhausted = (highWaterExists && (highWaterNext == 0U)) ||
                                      (highestReservedId == std::numeric_limits<U32>::max());
        this->m_idCounterExhausted = idSpaceExhausted;
        if (idSpaceExhausted) {
            this->m_nextProductId = 0U;
            if ((!highWaterExists || (highWaterNext != 0U)) &&
                !writeHighWaterUnlocked(this->m_storageRoot, 0U)) {
                return false;
            }
        } else {
            const U32 requiredNext = (highestReservedId == 0U) ? 1U : highestReservedId + 1U;
            this->m_nextProductId = (highWaterExists && (highWaterNext > requiredNext))
                ? highWaterNext
                : requiredNext;
            if ((!highWaterExists || (highWaterNext < this->m_nextProductId)) &&
                !writeHighWaterUnlocked(this->m_storageRoot, this->m_nextProductId)) {
                return false;
            }
        }
    }

    DIR* directory = ::opendir(this->m_storageRoot.c_str());
    if (directory == nullptr) {
        return false;
    }
    struct dirent* entry = nullptr;
    bool scanOk = true;
    for (;;) {
        errno = 0;
        entry = ::readdir(directory);
        if (entry == nullptr) {
            scanOk = (errno == 0);
            break;
        }
        U32 productId = 0U;
        if (!hasSuffix(entry->d_name, PRODUCT_METADATA_SUFFIX) ||
            !parseProductFileId(entry->d_name, productId)) {
            continue;
        }
        ProductRecord record{};
        if (!this->readMetadata(productId, record) || !this->isValidStoredProduct(record)) {
            const std::string expectedPath = productDataPath(this->m_storageRoot, productId);
            this->rejectProduct(productId, REJECT_INVALID_STORED_FILE, Fw::String(expectedPath.c_str()));
            continue;
        }
        if (this->m_storedProducts >= HISTORY_CAPACITY) {
            this->m_catalogOverflow = 1U;
            this->rejectProduct(productId, REJECT_CATALOG_FULL, record.sourcePath);
            continue;
        }
        this->m_history[this->m_storedProducts] = record;
        this->m_storedProducts += 1U;
    }
    (void)::closedir(directory);
    if (!scanOk) {
        return false;
    }
    std::sort(&this->m_history[0], &this->m_history[this->m_storedProducts],
              [](const ProductRecord& left, const ProductRecord& right) {
                  return left.productId < right.productId;
              });
    if (this->m_catalogOverflow != 0U) {
        return false;
    }
    return true;
}

bool StorageManager::persistNextProductId(U32 nextProductId) const {
    IdCounterLock counterLock(this->m_storageRoot);
    if (!counterLock.locked()) {
        return false;
    }
    const std::string markerPath = this->m_storageRoot + "/" + ID_HIGH_WATER_FILE;
    bool markerExists = false;
    U32 currentNext = 1U;
    if (!readHighWaterFile(markerPath, markerExists, currentNext)) {
        return false;
    }
    if (markerExists && (currentNext == 0U)) {
        nextProductId = 0U;
    } else if (markerExists && (nextProductId != 0U) && (currentNext > nextProductId)) {
        nextProductId = currentNext;
    }
    if (markerExists && (currentNext == nextProductId)) {
        return true;
    }
    return writeHighWaterUnlocked(this->m_storageRoot, nextProductId);
}

bool StorageManager::copyProductAtomically(const Fw::StringBase& sourcePath,
                                           const std::string& targetPath,
                                           U32 productBytes,
                                           U32 expectedCrc) const {
    const int sourceFd = ::open(sourcePath.toChar(), O_RDONLY);
    if (sourceFd < 0) {
        return false;
    }
    struct stat sourceInfo {};
    if ((::fstat(sourceFd, &sourceInfo) != 0) || !S_ISREG(sourceInfo.st_mode) ||
        (sourceInfo.st_size <= 0) || (static_cast<U64>(sourceInfo.st_size) != productBytes)) {
        (void)::close(sourceFd);
        return false;
    }
    if (::access(targetPath.c_str(), F_OK) == 0) {
        (void)::close(sourceFd);
        return false;
    }

    std::string temporaryPath;
    int targetFd = -1;
    for (U32 attempt = 0U; attempt < 8U; ++attempt) {
        temporaryPath = targetPath + ".tmp." + std::to_string(::getpid()) + "." + std::to_string(attempt);
        targetFd = ::open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0640);
        if (targetFd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            (void)::close(sourceFd);
            return false;
        }
    }
    if (targetFd < 0) {
        (void)::close(sourceFd);
        return false;
    }

    U16 crc = 0xFFFFU;
    U32 copied = 0U;
    U8 buffer[4096];
    bool ok = true;
    for (;;) {
        const ssize_t count = ::read(sourceFd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        if (count == 0) {
            break;
        }
        if (static_cast<U64>(copied) + static_cast<U64>(count) > productBytes) {
            ok = false;
            break;
        }
        updateCrc16(crc, buffer, static_cast<std::size_t>(count));
        if (!writeAll(targetFd, buffer, static_cast<std::size_t>(count))) {
            ok = false;
            break;
        }
        copied += static_cast<U32>(count);
    }
    ok = ok && (copied == productBytes) && (static_cast<U32>(crc) == expectedCrc) &&
         (::fchmod(targetFd, 0440) == 0) && (::fsync(targetFd) == 0);
    if (::close(sourceFd) != 0) {
        ok = false;
    }
    if (::close(targetFd) != 0) {
        ok = false;
    }
    if (!ok || (::link(temporaryPath.c_str(), targetPath.c_str()) != 0)) {
        (void)::unlink(temporaryPath.c_str());
        return false;
    }
    if (::unlink(temporaryPath.c_str()) != 0) {
        return false;
    }
    const int directoryFd = ::open(this->m_storageRoot.c_str(), O_RDONLY);
    if (directoryFd < 0) {
        return false;
    }
    const bool directorySynced = (::fsync(directoryFd) == 0);
    (void)::close(directoryFd);
    return directorySynced;
}

bool StorageManager::writeMetadataAtomically(const std::string& metadataPath,
                                             U32 productId,
                                             U32 productBytes,
                                             const Components::ScienceProductSource& sourceKind,
                                             U32 sourceCrc) const {
    char contents[96];
    const int length = std::snprintf(contents, sizeof(contents), "N2P1 %u %u %u %u\n",
                                     productId, productBytes, static_cast<U32>(sourceKind), sourceCrc);
    if ((length <= 0) || (static_cast<std::size_t>(length) >= sizeof(contents))) {
        return false;
    }
    std::string temporaryPath;
    int fd = -1;
    for (U32 attempt = 0U; attempt < 8U; ++attempt) {
        temporaryPath = metadataPath + ".tmp." + std::to_string(::getpid()) + "." + std::to_string(attempt);
        fd = ::open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0640);
        if (fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            return false;
        }
    }
    if (fd < 0) {
        return false;
    }
    bool ok = writeAll(fd, contents, static_cast<std::size_t>(length)) &&
              (::fchmod(fd, 0440) == 0) && (::fsync(fd) == 0);
    if (::close(fd) != 0) {
        ok = false;
    }
    if (!ok || (::link(temporaryPath.c_str(), metadataPath.c_str()) != 0)) {
        (void)::unlink(temporaryPath.c_str());
        return false;
    }
    if (::unlink(temporaryPath.c_str()) != 0) {
        return false;
    }
    const int directoryFd = ::open(this->m_storageRoot.c_str(), O_RDONLY);
    if (directoryFd < 0) {
        return false;
    }
    const bool directorySynced = (::fsync(directoryFd) == 0);
    (void)::close(directoryFd);
    return directorySynced;
}

bool StorageManager::readMetadata(U32 filenameProductId, ProductRecord& record) const {
    const std::string path = productMetadataPath(this->m_storageRoot, filenameProductId);
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    char line[96] = {};
    const bool readOk = (std::fgets(line, sizeof(line), file) != nullptr) &&
                        (std::fgetc(file) == EOF) && !std::ferror(file);
    (void)std::fclose(file);
    char magic[8] = {};
    unsigned long long productId = 0U;
    unsigned long long productBytes = 0U;
    unsigned long long sourceKind = 0U;
    unsigned long long sourceCrc = 0U;
    int consumed = 0;
    const int fields = std::sscanf(line, "%7s %llu %llu %llu %llu %n", magic,
                                   &productId, &productBytes, &sourceKind, &sourceCrc, &consumed);
    bool onlyWhitespaceRemains = true;
    for (const char* remainder = line + consumed; *remainder != '\0'; ++remainder) {
        if ((*remainder != ' ') && (*remainder != '\t') && (*remainder != '\r') && (*remainder != '\n')) {
            onlyWhitespaceRemains = false;
            break;
        }
    }
    if (!readOk || (fields != 5) || !onlyWhitespaceRemains || (std::strcmp(magic, "N2P1") != 0) ||
        (productId != filenameProductId) || (productBytes == 0U) ||
        (productBytes > std::numeric_limits<U32>::max()) ||
        (sourceKind == static_cast<U32>(Components::ScienceProductSource::UNKNOWN)) ||
        (sourceKind > static_cast<U32>(Components::ScienceProductSource::TEST)) ||
        (sourceCrc > 0xFFFFU)) {
        return false;
    }
    const std::string dataPath = productDataPath(this->m_storageRoot, filenameProductId);
    if (dataPath.size() >= SOURCE_PATH_CAPACITY) {
        return false;
    }
    record = ProductRecord{filenameProductId,
                           filenameProductId,
                           static_cast<U32>(productBytes),
                           static_cast<U32>(sourceCrc),
                           Components::ScienceProductSource(
                               static_cast<Components::ScienceProductSource::T>(sourceKind)),
                           Fw::String(dataPath.c_str())};
    return record.sourcePath.length() == dataPath.size();
}

bool StorageManager::isValidStoredProduct(const ProductRecord& record) const {
    if ((record.productId == 0U) || (record.productBytes == 0U) ||
        (record.sourceKind == Components::ScienceProductSource::UNKNOWN) ||
        (record.sourceCrc > 0xFFFFU) || (record.sourcePath.length() == 0U)) {
        return false;
    }
    U32 actualBytes = 0U;
    U32 actualCrc = 0U;
    return crcFile(record.sourcePath.toChar(), actualBytes, actualCrc) &&
           (actualBytes == record.productBytes) && (actualCrc == record.sourceCrc);
}

void StorageManager::rejectProduct(U32 productId, U32 reason, const Fw::StringBase& path) const {
    this->log_WARNING_HI_ScienceProductRejected(productId, reason);
    const Fw::String safePath(path.toChar());
    this->log_WARNING_LO_ScienceProductRejectedDetail(productId, reason, safePath);
}

void StorageManager::publishSelected(const ProductRecord& record, bool selectedByCommand) {
    this->m_selectedProductId = record.productId;
    this->tlmWrite_SelectedProductId(this->m_selectedProductId);
    if (selectedByCommand) {
        this->log_ACTIVITY_HI_ProductSelected(record.productId, record.productBytes,
                                               record.sourceCrc, record.sourcePath);
    }
    if (this->isConnected_downlinkReadyOut_OutputPort(0)) {
        this->downlinkReadyOut_out(0,
                                   record.productId,
                                   record.productBytes,
                                   record.sourceKind,
                                   record.sourcePath,
                                   record.sourceCrc);
    }
}

void StorageManager::reportLatest() const {
    if (this->m_storedProducts == 0U) {
        this->log_ACTIVITY_LO_StorageHistoryEmpty();
        return;
    }
    const ProductRecord& latest = this->m_history[this->m_storedProducts - 1U];
    this->log_ACTIVITY_HI_LatestDataset(this->m_storedProducts, latest.productBytes);
}

void StorageManager::reportHistory() const {
    if (this->m_storedProducts == 0U) {
        this->log_ACTIVITY_LO_StorageHistoryEmpty();
        return;
    }
    for (U32 slot = 0U; slot < this->m_storedProducts; ++slot) {
        const ProductRecord& record = this->m_history[slot];
        this->log_ACTIVITY_LO_StorageHistoryEntry(slot, slot + 1U, record.productBytes);
        this->log_ACTIVITY_LO_StorageCatalogEntry(slot, record.productId, record.productBytes,
                                                   record.sourceCrc, record.sourcePath);
    }
}

U32 StorageManager::historyDepth() const {
    return this->m_storedProducts;
}

StorageManager::ProductRecord* StorageManager::findProduct(U32 productId) {
    for (U32 index = 0U; index < this->m_storedProducts; ++index) {
        if (this->m_history[index].productId == productId) {
            return &this->m_history[index];
        }
    }
    return nullptr;
}

const StorageManager::ProductRecord* StorageManager::findProduct(U32 productId) const {
    for (U32 index = 0U; index < this->m_storedProducts; ++index) {
        if (this->m_history[index].productId == productId) {
            return &this->m_history[index];
        }
    }
    return nullptr;
}

void StorageManager::removeCaptureFiles(U32& removedFiles, U32& failedFiles) const {
    removedFiles = 0U;
    failedFiles = 0U;
    DIR* directory = ::opendir(TRANSIENT_CAPTURE_DIR);
    if (directory == nullptr) {
        return;
    }
    struct dirent* entry = nullptr;
    while ((entry = ::readdir(directory)) != nullptr) {
        const char* name = entry->d_name;
        if (!hasPrefix(name, TRANSIENT_CAPTURE_PREFIX) || !hasSuffix(name, TRANSIENT_CAPTURE_SUFFIX)) {
            continue;
        }
        const std::string path = std::string(TRANSIENT_CAPTURE_DIR) + "/" + name;
        if (::unlink(path.c_str()) == 0) {
            removedFiles += 1U;
        } else {
            failedFiles += 1U;
        }
    }
    (void)::closedir(directory);
}

}  // namespace Components
