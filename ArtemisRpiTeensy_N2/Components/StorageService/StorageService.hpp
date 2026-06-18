#ifndef Components_StorageService_HPP
#define Components_StorageService_HPP

#include "Components/StorageService/StorageServiceComponentAc.hpp"

namespace Components {

class StorageService final : public StorageServiceComponentBase {
  public:
    StorageService(const char* const compName);
    ~StorageService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum, U32 productBytes) override;
    void downlinkRequestIn_handler(FwIndexType portNum, U32 productBytes) override;
    void REPORT_STORAGE_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REPORT_LATEST_DATASET_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REPORT_STORAGE_HISTORY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REMOVE_OLD_DATASETS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 confirm) override;

    struct ProductRecord {
        U32 productCount;
        U32 productBytes;
    };

    void rememberProduct(U32 productCount, U32 productBytes);
    void reportLatest() const;
    void reportHistory() const;
    U32 historyDepth() const;
    void clearHistory();
    void removeCaptureFiles(U32& removedFiles, U32& failedFiles) const;

    static constexpr U32 HISTORY_CAPACITY = 16;
    static constexpr U32 REMOVE_CONFIRM = 1;
    U32 m_storedProducts;
    U32 m_lastProductSize;
    U32 m_historyNext;
    U32 m_historyCount;
    U32 m_removedDatasetFiles;
    U32 m_removeDatasetFailures;
    ProductRecord m_history[HISTORY_CAPACITY];
};

}  // namespace Components

#endif
