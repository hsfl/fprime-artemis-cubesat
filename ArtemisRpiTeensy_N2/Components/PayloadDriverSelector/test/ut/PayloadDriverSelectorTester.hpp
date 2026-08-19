#ifndef Components_PayloadDriverSelectorTester_HPP
#define Components_PayloadDriverSelectorTester_HPP

#include "Components/PayloadDriverSelector/PayloadDriverSelector.hpp"
#include "Components/PayloadDriverSelector/PayloadDriverSelectorGTestBase.hpp"

namespace Components {

class PayloadDriverSelectorTester final : public PayloadDriverSelectorGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    PayloadDriverSelectorTester();
    ~PayloadDriverSelectorTester();

    void testLeptonDefaultAndCompletion();
    void testBosonSelectionAndCompletion();
    void testSelectionRejectedWhileBusy();
    void testDuplicateRequestDoesNotTerminateActiveCapture();
    void testNonSelectedStatusIgnored();

  private:
    void from_driverRequestOut_handler(FwIndexType portNum, U32 durationSeconds) override;
    void from_deactivateDriverOut_handler(FwIndexType portNum) override;
    void connectPorts();
    void initComponents();

    FwIndexType m_lastDriverRequestPort = 0;
    FwIndexType m_lastDeactivatedDriverPort = 0;
    PayloadDriverSelector component;
};

}  // namespace Components

#endif
