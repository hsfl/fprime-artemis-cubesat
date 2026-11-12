#pragma once
#include "FprimeArtemisCore/Components/EpsManager/EpsManager.hpp"
#include "FprimeArtemisCore/Components/EpsManager/EpsManagerGTestBase.hpp"
#include <vector>

namespace Components {
class EpsManagerTester final : public EpsManagerGTestBase {
  public:
    EpsManagerTester();
    void nominalAndBusy();
    void readbackAndCycle();
    void faultsAndStaleness();
    Drv::ByteStreamStatus from_drvSendOut_handler(FwIndexType, Fw::Buffer&) override;
  private:
    static constexpr FwSizeType MAX_HISTORY_SIZE = 100;
    static constexpr FwEnumStoreType TEST_INSTANCE_ID = 0;
    void initComponents();
    void connectPorts();
    void reply(const std::vector<U8>& payload, U8 status = PDU_V2_STATUS_OK, bool corrupt = false);
    void tick(unsigned count);
    EpsManager component;
    std::vector<U8> request;
};
}
