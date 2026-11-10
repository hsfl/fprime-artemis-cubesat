#include "SoHAppTester.hpp"

namespace Components {

namespace {
using Health = Components::HealthState;
constexpr U8 SKIP_NONE = 0x00;
constexpr U8 SKIP_COMMS_AND_TRANSPORT = 0xC0;  // slots 6 and 7: absent in the CDR build / not on N2's path
constexpr U8 SKIP_GPS = 0x08;                  // slot 3
}  // namespace

SoHAppTester::SoHAppTester()
    : SoHAppGTestBase("SoHAppTester", MAX_HISTORY_SIZE),
      component("SoHApp") {
    this->initComponents();
    this->connectPorts();
}

SoHAppTester::~SoHAppTester() {
    this->component.deinit();
}

void SoHAppTester::reportAll(Components::HealthState health, U8 skipMask) {
    for (U8 slot = 0; slot < 8U; ++slot) {
        if (((skipMask >> slot) & 1U) == 0U) {
            this->invoke_to_statusIn(slot, health, 100U + slot);
        }
    }
}

void SoHAppTester::runTicks(U32 ticks) {
    for (U32 tick = 0; tick < ticks; ++tick) {
        this->invoke_to_run(0, 0);
    }
}

void SoHAppTester::setReporting(U32 cmdSeq, Fw::Enabled state) {
    const FwSizeType before = this->cmdResponseHistory->size();
    this->sendCmd_SET_SOH_REPORTING(0, cmdSeq, state);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(before + 1);
    ASSERT_CMD_RESPONSE(before, SoHAppComponentBase::OPCODE_SET_SOH_REPORTING, cmdSeq, Fw::CmdResponse::OK);
}

void SoHAppTester::emitSnapshot(U32 cmdSeq) {
    const FwSizeType before = this->cmdResponseHistory->size();
    this->sendCmd_EMIT_SOH_SNAPSHOT(0, cmdSeq);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(before + 1);
    ASSERT_CMD_RESPONSE(before, SoHAppComponentBase::OPCODE_EMIT_SOH_SNAPSHOT, cmdSeq, Fw::CmdResponse::OK);
}

void SoHAppTester::testEmitSnapshotReportsEverySlot() {
    this->reportAll(Health::OK, SKIP_NONE);
    this->clearHistory();

    ASSERT_NO_FATAL_FAILURE(this->emitSnapshot(1));

    // One Snapshot event and one value on every channel.
    ASSERT_EVENTS_Snapshot_SIZE(1);
    ASSERT_EVENTS_Snapshot(0, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK,
                           Health::OK, Health::OK);
    ASSERT_TLM_OverallHealth(0, Health::OK);
    ASSERT_TLM_EpsHealth(0, Health::OK);
    ASSERT_TLM_PayloadHealth(0, Health::OK);
    ASSERT_TLM_AdcsHealth(0, Health::OK);
    ASSERT_TLM_GpsHealth(0, Health::OK);
    ASSERT_TLM_StorageHealth(0, Health::OK);
    ASSERT_TLM_ThermalHealth(0, Health::OK);
    ASSERT_TLM_CommsHealth(0, Health::OK);
    ASSERT_TLM_TransportHealth(0, Health::OK);
    ASSERT_TLM_SohReporting(0, Fw::Enabled::DISABLED);
}

void SoHAppTester::testOverallIgnoresMissingOptionalSlots() {
    // The CDR build has no CommsApp, and N2 has no Teensy transport: those slots never report.
    this->reportAll(Health::OK, SKIP_COMMS_AND_TRANSPORT);
    this->clearHistory();

    ASSERT_NO_FATAL_FAILURE(this->emitSnapshot(1));

    // Before this change OverallHealth was stuck at UNKNOWN here. Now the six required slots decide.
    ASSERT_EVENTS_Snapshot(0, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK,
                           Health::UNKNOWN, Health::UNKNOWN);
}

void SoHAppTester::testOverallUnknownWhenRequiredSlotSilent() {
    // GPS is required. If it never reports, the spacecraft cannot claim OK.
    this->reportAll(Health::OK, SKIP_GPS);
    this->clearHistory();

    ASSERT_NO_FATAL_FAILURE(this->emitSnapshot(1));

    ASSERT_EVENTS_Snapshot(0, Health::UNKNOWN, Health::OK, Health::OK, Health::OK, Health::UNKNOWN, Health::OK,
                           Health::OK, Health::OK, Health::OK);
}

void SoHAppTester::testFailThenWarnWin() {
    this->reportAll(Health::OK, SKIP_COMMS_AND_TRANSPORT);
    this->clearHistory();

    // WARN on a required slot.
    this->invoke_to_statusIn(1, Health::WARN, 7);
    ASSERT_NO_FATAL_FAILURE(this->emitSnapshot(1));
    ASSERT_EVENTS_Snapshot(0, Health::WARN, Health::OK, Health::WARN, Health::OK, Health::OK, Health::OK,
                           Health::OK, Health::UNKNOWN, Health::UNKNOWN);

    // FAIL on an optional slot still counts once it reports: FAIL beats WARN.
    this->invoke_to_statusIn(6, Health::FAIL, 9);
    ASSERT_NO_FATAL_FAILURE(this->emitSnapshot(2));
    ASSERT_EVENTS_Snapshot(1, Health::FAIL, Health::OK, Health::WARN, Health::OK, Health::OK, Health::OK,
                           Health::OK, Health::FAIL, Health::UNKNOWN);
}

void SoHAppTester::testRequiredSlotsParameter() {
    // All eight required: the silent comms slot holds OverallHealth at UNKNOWN, as before this change.
    this->paramSet_SOH_REQUIRED_SLOTS(0xFF, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->reportAll(Health::OK, 0x40);
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->emitSnapshot(1));
    ASSERT_EVENTS_Snapshot(0, Health::UNKNOWN, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK,
                           Health::OK, Health::UNKNOWN, Health::OK);

    // Only EPS required: the rest may stay silent.
    this->paramSet_SOH_REQUIRED_SLOTS(0x01, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->reportAll(Health::UNKNOWN, 0x01);
    ASSERT_NO_FATAL_FAILURE(this->emitSnapshot(2));
    ASSERT_EVENTS_Snapshot(1, Health::OK, Health::OK, Health::UNKNOWN, Health::UNKNOWN, Health::UNKNOWN,
                           Health::UNKNOWN, Health::UNKNOWN, Health::UNKNOWN, Health::UNKNOWN);
}

void SoHAppTester::testReportingOffByDefaultSendsNothing() {
    // The RF-budget rule: with reporting off, the 1 Hz tick sends nothing at all.
    this->reportAll(Health::OK, SKIP_NONE);
    this->clearHistory();
    this->runTicks(30);
    ASSERT_EVENTS_SIZE(0);
    ASSERT_TLM_SIZE(0);
}

void SoHAppTester::testReportingOnSendsNowThenEveryPeriod() {
    this->reportAll(Health::OK, SKIP_COMMS_AND_TRANSPORT);
    this->clearHistory();

    ASSERT_NO_FATAL_FAILURE(this->setReporting(1, Fw::Enabled::ENABLED));
    ASSERT_EVENTS_SohReportingChanged_SIZE(1);
    ASSERT_EVENTS_SohReportingChanged(0, Fw::Enabled::ENABLED, 10);
    ASSERT_EVENTS_Snapshot_SIZE(1);  // the first report goes out at once
    ASSERT_TLM_SohReporting(0, Fw::Enabled::ENABLED);

    this->runTicks(9);
    ASSERT_EVENTS_Snapshot_SIZE(1);  // 9 of 10 seconds: not yet
    this->runTicks(1);
    ASSERT_EVENTS_Snapshot_SIZE(2);  // 10 seconds: the next report
    this->runTicks(10);
    ASSERT_EVENTS_Snapshot_SIZE(3);
    ASSERT_EVENTS_Snapshot(2, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK, Health::OK,
                           Health::UNKNOWN, Health::UNKNOWN);
}

void SoHAppTester::testReportingOffStops() {
    this->reportAll(Health::OK, SKIP_NONE);
    this->clearHistory();

    ASSERT_NO_FATAL_FAILURE(this->setReporting(1, Fw::Enabled::ENABLED));
    ASSERT_NO_FATAL_FAILURE(this->setReporting(2, Fw::Enabled::DISABLED));
    ASSERT_EVENTS_SohReportingChanged(1, Fw::Enabled::DISABLED, 10);
    this->runTicks(30);

    ASSERT_EVENTS_Snapshot_SIZE(1);  // only the one sent when reporting was turned on
    ASSERT_TLM_SohReporting_SIZE(2);
    ASSERT_TLM_SohReporting(0, Fw::Enabled::ENABLED);
    ASSERT_TLM_SohReporting(1, Fw::Enabled::DISABLED);
}

void SoHAppTester::testPeriodParameter() {
    this->paramSet_SOH_PERIOD_SECONDS(3, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->reportAll(Health::OK, SKIP_NONE);
    this->clearHistory();

    ASSERT_NO_FATAL_FAILURE(this->setReporting(1, Fw::Enabled::ENABLED));
    ASSERT_EVENTS_SohReportingChanged(0, Fw::Enabled::ENABLED, 3);
    this->runTicks(3);
    ASSERT_EVENTS_Snapshot_SIZE(2);
    this->runTicks(3);
    ASSERT_EVENTS_Snapshot_SIZE(3);
    this->runTicks(2);
    ASSERT_EVENTS_Snapshot_SIZE(3);
}

void SoHAppTester::testBadPeriodRejectedAndDefaultUsed() {
    this->clearHistory();

    // The ground sets 0 s. F' stores it, SoHApp warns and keeps using the 10 s default.
    this->paramSet_SOH_PERIOD_SECONDS(0, Fw::ParamValid::VALID);
    this->paramSend_SOH_PERIOD_SECONDS(0, 7);
    ASSERT_CMD_RESPONSE(0, SoHAppComponentBase::OPCODE_SOH_PERIOD_SECONDS_SET, 7, Fw::CmdResponse::OK);
    ASSERT_EVENTS_SohParameterRejected_SIZE(1);
    ASSERT_EVENTS_SohParameterRejected(
        0, static_cast<U32>(this->component.getIdBase() + SoHAppComponentBase::PARAMID_SOH_PERIOD_SECONDS), 0);

    ASSERT_NO_FATAL_FAILURE(this->setReporting(8, Fw::Enabled::ENABLED));
    ASSERT_EVENTS_SohReportingChanged(0, Fw::Enabled::ENABLED, 10);
    this->runTicks(9);
    ASSERT_EVENTS_Snapshot_SIZE(1);
    this->runTicks(1);
    ASSERT_EVENTS_Snapshot_SIZE(2);
}

void SoHAppTester::testStatusChangeShowsInNextReport() {
    this->reportAll(Health::OK, SKIP_NONE);
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->setReporting(1, Fw::Enabled::ENABLED));

    // EPS drops to WARN between reports. The next periodic report carries it.
    this->invoke_to_statusIn(0, Health::WARN, 42);
    this->runTicks(10);
    ASSERT_EVENTS_Snapshot_SIZE(2);
    ASSERT_EVENTS_Snapshot(1, Health::WARN, Health::WARN, Health::OK, Health::OK, Health::OK, Health::OK,
                           Health::OK, Health::OK, Health::OK);
    ASSERT_TLM_EpsHealth_SIZE(2);
    ASSERT_TLM_EpsHealth(1, Health::WARN);
}

void SoHAppTester::testExistingIdsUnchanged() {
    // Items that existed before this change keep their numbers. New ones were appended.
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::OPCODE_EMIT_SOH_SNAPSHOT), 0x0U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::EVENTID_SNAPSHOT), 0x0U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::CHANNELID_OVERALLHEALTH), 0x0U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::CHANNELID_EPSHEALTH), 0x1U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::CHANNELID_PAYLOADHEALTH), 0x2U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::CHANNELID_ADCSHEALTH), 0x3U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::CHANNELID_GPSHEALTH), 0x4U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::CHANNELID_STORAGEHEALTH), 0x5U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::CHANNELID_THERMALHEALTH), 0x6U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::CHANNELID_COMMSHEALTH), 0x7U);
    ASSERT_EQ(static_cast<U32>(SoHAppComponentBase::CHANNELID_TRANSPORTHEALTH), 0x8U);
}

}  // namespace Components
