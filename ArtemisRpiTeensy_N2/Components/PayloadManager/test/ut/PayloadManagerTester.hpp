#ifndef Components_PayloadManagerTester_HPP
#define Components_PayloadManagerTester_HPP

#include "Components/PayloadManager/PayloadManager.hpp"
#include "Components/PayloadManager/PayloadManagerGTestBase.hpp"

namespace Components {

class PayloadManagerTester final : public PayloadManagerGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    PayloadManagerTester();
    ~PayloadManagerTester();

    void testDirectCaptureCommandsRefusedWhileInhibited();
    void testScienceRequestRefusedWhileInhibited();
    void testPassthroughRefusedWhileInhibited();
    void testCapturesResumeWhenEnabled();
    void testRunningCaptureFinishesUnderInhibit();
    void testRequestQueuedAfterInhibitIsRefused();
    void testCapturesStartBeforeAnyControlArrives();

  private:
    void connectPorts();
    void initComponents();
    //! Sends a capture control, dispatches it, and checks the report back.
    void applyControl(Fw::Enabled control, bool activeOperation);

    PayloadManager component;
};

}  // namespace Components

#endif
