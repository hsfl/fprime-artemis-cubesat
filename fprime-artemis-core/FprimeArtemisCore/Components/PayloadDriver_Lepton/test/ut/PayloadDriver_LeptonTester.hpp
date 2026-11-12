#pragma once
#include "FprimeArtemisCore/Components/PayloadDriver_Lepton/PayloadDriver_Lepton.hpp"
#include "FprimeArtemisCore/Components/PayloadDriver_Lepton/PayloadDriver_LeptonGTestBase.hpp"
#include <vector>

namespace Components {
class PayloadDriver_LeptonTester final : public PayloadDriver_LeptonGTestBase {
  public:
    PayloadDriver_LeptonTester();
    ~PayloadDriver_LeptonTester();
    void cancelLongCaptureThenFresh();
    void cancelPendingWrite();
    void cancelledWriteTimeoutRejectsLateResult();
    void completionRacingCancel();
    Fw::Success::T productGet_handler(FwDpIdType, FwSizeType, Fw::Buffer&) override;
    void productSend_handler(FwDpIdType, const Fw::Buffer&) override;
    void from_cancelDoneOut_handler(FwIndexType) override;
  private:
    static constexpr FwSizeType MAX_HISTORY_SIZE = 100;
    static constexpr FwEnumStoreType TEST_INSTANCE_ID = 0;
    static constexpr FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 16;
    void initComponents();
    void connectPorts();
    void dispatchRun();
    void completeInterval();
    void acknowledge(const std::string& path);
    void startShortCapture();
    PayloadDriver_Lepton component;
    std::vector<U8> buffer;
    std::string previousDirectory;
    std::string directory;
    std::vector<std::string> files;
};
}
