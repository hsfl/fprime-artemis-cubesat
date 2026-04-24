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
    void requestIn_handler(FwIndexType portNum, U32 key) override;
    void downlinkRequestIn_handler(FwIndexType portNum, U32 key) override;
    void REPORT_STORAGE_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    U32 m_storedProducts;
    U32 m_lastProductSize;
};

}  // namespace Components

#endif
