#ifndef Components_SoHAppTester_HPP
#define Components_SoHAppTester_HPP

#include "Components/SoHApp/SoHApp.hpp"
#include "Components/SoHApp/SoHAppGTestBase.hpp"

namespace Components {

class SoHAppTester final : public SoHAppGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 64;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    SoHAppTester();
    ~SoHAppTester();

    // What a report says
    void testEmitSnapshotReportsEverySlot();
    void testOverallIgnoresMissingOptionalSlots();
    void testOverallUnknownWhenRequiredSlotSilent();
    void testFailThenWarnWin();
    void testRequiredSlotsParameter();
    // When reports go out
    void testReportingOffByDefaultSendsNothing();
    void testReportingOnSendsNowThenEveryPeriod();
    void testReportingOffStops();
    void testPeriodParameter();
    void testBadPeriodRejectedAndDefaultUsed();
    void testStatusChangeShowsInNextReport();
    void testExistingIdsUnchanged();

  private:
    void connectPorts();
    void initComponents();
    //! Every slot reports `health`, except slots whose bit is set in `skipMask` (they stay silent).
    void reportAll(Components::HealthState health, U8 skipMask);
    //! Runs `ticks` 1 Hz rate-group calls (run is a sync port: each call runs at once).
    void runTicks(U32 ticks);
    //! Sends SET_SOH_REPORTING and checks it replied OK.
    void setReporting(U32 cmdSeq, Fw::Enabled state);
    //! Sends EMIT_SOH_SNAPSHOT and checks it replied OK.
    void emitSnapshot(U32 cmdSeq);

    SoHApp component;
};

}  // namespace Components

#endif
