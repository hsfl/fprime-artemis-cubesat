#ifndef Components_StorageManager_HPP
#define Components_StorageManager_HPP

#include "Components/StorageManager/StorageManagerComponentAc.hpp"

#include <string>

namespace Components {

class StorageManager final : public StorageManagerComponentBase {
  public:
    StorageManager(const char* const compName);
    ~StorageManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum,
                           U32 productId,
                           U32 productBytes,
                           const Components::ScienceProductSource& sourceKind,
                           const Fw::StringBase& sourcePath,
                           U32 sourceCrc) override;
    void deliveryRequestIn_handler(FwIndexType portNum, U32 productId) override;
    void downlinkRequestIn_handler(FwIndexType portNum,
                                   U32 productId,
                                   U32 productBytes,
                                   const Components::ScienceProductSource& sourceKind,
                                   const Fw::StringBase& sourcePath,
                                   U32 sourceCrc) override;
    void REPORT_STORAGE_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REPORT_LATEST_DATASET_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REPORT_STORAGE_HISTORY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REMOVE_OLD_DATASETS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 confirm) override;
    void SELECT_PRODUCT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 productId) override;

    struct ProductRecord {
        U32 productId;
        U32 inputProductId;
        U32 productBytes;
        U32 sourceCrc;
        Components::ScienceProductSource sourceKind;
        Fw::String sourcePath;
    };

    bool ensureCatalogLoaded();
    bool loadCatalog();
    bool persistNextProductId(U32 nextProductId) const;
    bool copyProductAtomically(const Fw::StringBase& sourcePath,
                               const std::string& targetPath,
                               U32 productBytes,
                               U32 expectedCrc) const;
    bool writeMetadataAtomically(const std::string& metadataPath,
                                 U32 productId,
                                 U32 productBytes,
                                 const Components::ScienceProductSource& sourceKind,
                                 U32 sourceCrc) const;
    bool readMetadata(U32 filenameProductId, ProductRecord& record) const;
    bool isValidStoredProduct(const ProductRecord& record) const;
    void rejectProduct(U32 productId, U32 reason, const Fw::StringBase& path) const;
    void rejectIncomingProduct(U32 productId, U32 reason, const Fw::StringBase& path);
    void publishSelected(const ProductRecord& record, bool selectedByCommand);
    void reportLatest() const;
    void reportHistory() const;
    U32 historyDepth() const;
    ProductRecord* findProduct(U32 productId);
    const ProductRecord* findProduct(U32 productId) const;
    void removeCaptureFiles(U32& removedFiles, U32& failedFiles) const;

    static constexpr U32 HISTORY_CAPACITY = 16;
    static constexpr U32 REMOVE_CONFIRM = 1;
    U32 m_storedProducts;
    U32 m_lastProductSize;
    U32 m_selectedProductId;
    U32 m_nextProductId;
    U32 m_removedDatasetFiles;
    U32 m_removeDatasetFailures;
    U32 m_catalogOverflow;
    bool m_catalogLoaded;
    bool m_storageReady;
    bool m_idCounterExhausted;
    bool m_recoveryReported;
    std::string m_storageRoot;
    ProductRecord m_history[HISTORY_CAPACITY];
};

}  // namespace Components

#endif
