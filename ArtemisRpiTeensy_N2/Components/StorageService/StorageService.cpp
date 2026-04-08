#include "Components/StorageService/StorageService.hpp"

namespace Components {

StorageService::StorageService(const char* const compName)
    : StorageServiceComponentBase(compName),
      m_storedProducts(0),
      m_lastProductSize(0) {}

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
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_storedProducts);
    }
}

void StorageService::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_storedProducts += 1;
    this->m_lastProductSize = key;
    this->log_ACTIVITY_HI_ScienceStored(this->m_storedProducts, this->m_lastProductSize);
    if (this->isConnected_downlinkReadyOut_OutputPort(0)) {
        this->downlinkReadyOut_out(0, this->m_lastProductSize);
    }
}

void StorageService::downlinkRequestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    static_cast<void>(key);
    this->log_ACTIVITY_LO_DownlinkPrepared(this->m_lastProductSize);
    if (this->isConnected_downlinkReadyOut_OutputPort(0)) {
        this->downlinkReadyOut_out(0, this->m_lastProductSize);
    }
}

void StorageService::REPORT_STORAGE_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_ACTIVITY_LO_DownlinkPrepared(this->m_lastProductSize);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
