#include "CommsAppTester.hpp"

namespace Components {

CommsAppTester::CommsAppTester()
    : CommsAppGTestBase("CommsAppTester", MAX_HISTORY_SIZE),
      component("CommsApp") {
    this->initComponents();
    this->connectPorts();
}

CommsAppTester::~CommsAppTester() {
    this->component.deinit();
}

void CommsAppTester::testRejectsDownlinkWithoutScience() {
    this->clearHistory();

    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 0);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, CommsAppComponentBase::OPCODE_REQUEST_SCIENCE_DOWNLINK, 0,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_CommsCommandRejected_SIZE(1);
    ASSERT_EVENTS_CommsCommandRejected(0, 1, 0);
    ASSERT_EVENTS_DownlinkFailed_SIZE(0);
    ASSERT_from_payloadDownlinkRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
}

void CommsAppTester::testCompletedDownlinkRemainsAvailableForRetry() {
    this->clearHistory();

    Fw::String sourcePath("/tmp/science.bin");
    this->invoke_to_scienceReadyIn(0, 7, 128, Components::ScienceProductSource::NEUTRON_SIM, sourcePath, 0x1234);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 0);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, CommsAppComponentBase::OPCODE_REQUEST_SCIENCE_DOWNLINK, 0,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_DownlinkRequested_SIZE(1);
    ASSERT_EVENTS_DownlinkRequested(0, 128);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::DOWNLINKING, 128);
    ASSERT_from_payloadDownlinkRequestOut_SIZE(1);
    ASSERT_from_payloadDownlinkRequestOut(0, 7, 128, Components::ScienceProductSource::NEUTRON_SIM, sourcePath, 0x1234);

    this->invoke_to_payloadDownlinkStatusIn(0, 1, 1, 7, 128, 1, 4, 0);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 2, 1, 7, 128, 4, 4, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_DownlinkFinished_SIZE(1);
    ASSERT_EVENTS_DownlinkFinished(0, 128);
    ASSERT_from_missionModeOut_SIZE(2);
    ASSERT_from_missionModeOut(1, Components::MissionMode::BASE, 128);

    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 1);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, CommsAppComponentBase::OPCODE_REQUEST_SCIENCE_DOWNLINK, 1,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_DownlinkRequested_SIZE(2);
    ASSERT_EVENTS_DownlinkRequested(1, 128);
    ASSERT_from_payloadDownlinkRequestOut_SIZE(2);
    ASSERT_from_payloadDownlinkRequestOut(1, 7, 128, Components::ScienceProductSource::NEUTRON_SIM, sourcePath, 0x1234);
    ASSERT_from_missionModeOut_SIZE(3);
    ASSERT_from_missionModeOut(2, Components::MissionMode::DOWNLINKING, 128);
}

void CommsAppTester::testDuplicateAndConflictingActiveRequestsAreGuarded() {
    this->clearHistory();

    Fw::String sourcePath("/tmp/science.bin");
    this->invoke_to_scienceReadyIn(0, 7, 128, Components::ScienceProductSource::NEUTRON_SIM, sourcePath, 0x1234);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 0);
    this->component.doDispatch();

    this->invoke_to_payloadDownlinkStatusIn(0, 1, 41, 7, 128, 2, 4, 0);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 1);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, CommsAppComponentBase::OPCODE_REQUEST_SCIENCE_DOWNLINK, 1,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_DownlinkRequested_SIZE(1);
    ASSERT_EVENTS_DownlinkRequestDuplicate_SIZE(1);
    ASSERT_EVENTS_DownlinkRequestDuplicate(0, 7, 128);
    ASSERT_from_payloadDownlinkRequestOut_SIZE(1);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_TLM_DownlinkActive(0, 1);
    ASSERT_TLM_ActiveDownlinkProductId(0, 7);
    ASSERT_TLM_ActiveDownlinkTransferId(1, 41);
    ASSERT_TLM_DownlinkRequestDisposition(1, 1);

    Fw::String conflictingPath("/tmp/revised-science.bin");
    this->invoke_to_scienceReadyIn(0, 7, 256, Components::ScienceProductSource::TEST, conflictingPath, 0x5678);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 2);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(3);
    ASSERT_CMD_RESPONSE(2, CommsAppComponentBase::OPCODE_REQUEST_SCIENCE_DOWNLINK, 2,
                        Fw::CmdResponse::BUSY);
    ASSERT_EVENTS_DownlinkRequestConflict_SIZE(1);
    ASSERT_EVENTS_DownlinkRequestConflict(0, 7, 7);
    ASSERT_EVENTS_CommsCommandRejected_SIZE(1);
    ASSERT_EVENTS_CommsCommandRejected(0, 2, 7);
    ASSERT_from_payloadDownlinkRequestOut_SIZE(1);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_TLM_DownlinkRequestDisposition(2, 2);
}

void CommsAppTester::testNewPendingProductStartsAfterActiveTerminalStatus() {
    this->clearHistory();

    Fw::String firstPath("/tmp/science.bin");
    this->invoke_to_scienceReadyIn(0, 7, 128, Components::ScienceProductSource::NEUTRON_SIM, firstPath, 0x1234);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 0);
    this->component.doDispatch();

    Fw::String nextPath("/tmp/next-science.bin");
    this->invoke_to_scienceReadyIn(0, 8, 256, Components::ScienceProductSource::TEST, nextPath, 0x5678);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 1, 41, 7, 128, 1, 4, 0);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 2, 41, 7, 128, 4, 4, 0);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 1);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, CommsAppComponentBase::OPCODE_REQUEST_SCIENCE_DOWNLINK, 1,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_DownlinkFinished_SIZE(1);
    ASSERT_EVENTS_DownlinkRequested_SIZE(2);
    ASSERT_from_payloadDownlinkRequestOut_SIZE(2);
    ASSERT_from_payloadDownlinkRequestOut(1, 8, 256, Components::ScienceProductSource::TEST, nextPath, 0x5678);
    ASSERT_from_missionModeOut_SIZE(3);
    ASSERT_from_missionModeOut(1, Components::MissionMode::BASE, 128);
    ASSERT_from_missionModeOut(2, Components::MissionMode::DOWNLINKING, 256);
}

void CommsAppTester::testStaleOrUncorrelatedStatusCannotEndActiveTransfer() {
    this->clearHistory();

    Fw::String sourcePath("/tmp/science.bin");
    this->invoke_to_scienceReadyIn(0, 7, 128, Components::ScienceProductSource::NEUTRON_SIM, sourcePath, 0x1234);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 0);
    this->component.doDispatch();

    // Terminal-first, wrong-size, unknown-state, and zero-ID status packets
    // cannot establish ownership of this newly requested transfer.
    this->invoke_to_payloadDownlinkStatusIn(0, 2, 41, 7, 128, 4, 4, 0);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 1, 41, 7, 127, 1, 4, 0);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 9, 41, 7, 128, 1, 4, 0);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 1, 0, 7, 128, 1, 4, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_PayloadDownlinkStatusIgnored_SIZE(4);
    ASSERT_EVENTS_DownlinkFinished_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(1);

    // The first valid active status establishes transfer 42. A stale terminal
    // for transfer 41 remains harmless; only transfer 42 may finish it.
    this->invoke_to_payloadDownlinkStatusIn(0, 1, 42, 7, 128, 1, 4, 0);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 2, 41, 7, 128, 4, 4, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_DownlinkFinished_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(1);

    this->invoke_to_payloadDownlinkStatusIn(0, 2, 42, 7, 128, 4, 4, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_DownlinkFinished_SIZE(1);
    ASSERT_from_missionModeOut_SIZE(2);
    ASSERT_from_missionModeOut(1, Components::MissionMode::BASE, 128);
}

void CommsAppTester::testDownlinkFailureReturnsBase() {
    this->clearHistory();

    Fw::String sourcePath("/tmp/science.bin");
    this->invoke_to_scienceReadyIn(0, 8, 64, Components::ScienceProductSource::TEST, sourcePath, 0x55);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 0);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 1, 2, 8, 64, 1, 2, 0);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 3, 2, 8, 64, 1, 2, 99);
    this->component.doDispatch();

    ASSERT_EVENTS_DownlinkFailed_SIZE(1);
    ASSERT_EVENTS_DownlinkFailed(0, 3, 99);
    ASSERT_from_missionModeOut_SIZE(2);
    ASSERT_from_missionModeOut(1, Components::MissionMode::BASE, 99);
}

void CommsAppTester::testIgnoresPayloadStatusWhenInactive() {
    this->clearHistory();

    this->invoke_to_payloadDownlinkStatusIn(0, 2, 1, 7, 128, 4, 4, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_DownlinkFinished_SIZE(0);
    ASSERT_EVENTS_DownlinkFailed_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
}

void CommsAppTester::testDriverStatusPollingAndRssiPing() {
    this->clearHistory();

    this->sendCmd_REQUEST_LINK_STATUS(0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_from_driverRequestOut_SIZE(1);
    ASSERT_from_driverRequestOut(0, Components::RadioOperation::STATUS, 0);

    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::STATUS,
                                   Components::RadioRpcResult::OK,
                                   Components::RadioState::READY,
                                   Components::RadioFault::NONE,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   1,
                                   4,
                                   3,
                                   0);
    this->component.doDispatch();
    ASSERT_EVENTS_RadioStatusUpdated_SIZE(1);
    ASSERT_EVENTS_RadioStatusUpdated(0,
                                     Components::RadioState::READY,
                                     Components::RadioFault::NONE,
                                     Components::RadioRpcResult::OK);

    this->sendCmd_PING_LINK_RSSI(0, 1);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_from_driverRequestOut_SIZE(2);
    ASSERT_from_driverRequestOut(1, Components::RadioOperation::STATUS, 0);

    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::STATUS,
                                   Components::RadioRpcResult::OK,
                                   Components::RadioState::READY,
                                   Components::RadioFault::NONE,
                                   0,
                                   1,
                                   -72,
                                   100,
                                   2,
                                   5,
                                   4,
                                   0);
    this->component.doDispatch();
    ASSERT_EVENTS_RadioStatusUpdated_SIZE(2);
    ASSERT_EVENTS_RadioStatusUpdated(1,
                                     Components::RadioState::READY,
                                     Components::RadioFault::NONE,
                                     Components::RadioRpcResult::OK);
    ASSERT_EVENTS_LinkRssiPing_SIZE(1);
    ASSERT_EVENTS_LinkRssiPing(0, Components::RadioState::READY, 1, -72, 100, 2);
}

void CommsAppTester::testBootReconcilesOffToReadyAndPublishesHealth() {
    this->clearHistory();

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_sohStatusOut_SIZE(1);
    ASSERT_from_sohStatusOut(0, Components::HealthState::UNKNOWN, 0);
    ASSERT_from_driverRequestOut_SIZE(1);
    ASSERT_from_driverRequestOut(0, Components::RadioOperation::STATUS, 0);

    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::STATUS,
                                   Components::RadioRpcResult::OK,
                                   Components::RadioState::OFF,
                                   Components::RadioFault::NONE,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   1,
                                   0,
                                   0,
                                   0);
    this->component.doDispatch();
    ASSERT_from_sohStatusOut_SIZE(2);
    ASSERT_from_sohStatusOut(1, Components::HealthState::WARN, 0);
    ASSERT_from_driverRequestOut_SIZE(2);
    ASSERT_from_driverRequestOut(1, Components::RadioOperation::SET_ENABLED, 1);

    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::SET_ENABLED,
                                   Components::RadioRpcResult::OK,
                                   Components::RadioState::READY,
                                   Components::RadioFault::NONE,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   2,
                                   0,
                                   0,
                                   0);
    this->component.doDispatch();
    ASSERT_from_sohStatusOut_SIZE(3);
    ASSERT_from_sohStatusOut(2, Components::HealthState::OK, 0x100);
    ASSERT_EVENTS_RadioRecoveryScheduled_SIZE(0);
}

void CommsAppTester::testReadyWithLocalFaultIsDegradedAndReinitialized() {
    this->clearHistory();

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_driverRequestOut_SIZE(1);
    ASSERT_from_driverRequestOut(0, Components::RadioOperation::STATUS, 0);

    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::STATUS,
                                   Components::RadioRpcResult::OK,
                                   Components::RadioState::READY,
                                   Components::RadioFault::LOCAL_TX_FAULT,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   1,
                                   0,
                                   0,
                                   1);
    this->component.doDispatch();

    ASSERT_from_sohStatusOut_SIZE(2);
    ASSERT_from_sohStatusOut(1, Components::HealthState::WARN, 0x103);
    ASSERT_from_driverRequestOut_SIZE(2);
    ASSERT_from_driverRequestOut(1, Components::RadioOperation::SET_ENABLED, 1);
    ASSERT_EVENTS_RadioRecoveryScheduled_SIZE(0);
}

void CommsAppTester::testRecoveryBackoffIsCappedAndStatusIsObservational() {
    this->clearHistory();

    // Boot reconciliation first observes factual OFF, then makes one immediate
    // enable attempt. Only that failed SET attempt starts the retry ladder.
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::STATUS,
                                   Components::RadioRpcResult::OK,
                                   Components::RadioState::OFF,
                                   Components::RadioFault::NONE,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   1,
                                   0,
                                   0,
                                   0);
    this->component.doDispatch();
    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::SET_ENABLED,
                                   Components::RadioRpcResult::TARGET_ERROR,
                                   Components::RadioState::OFF,
                                   Components::RadioFault::INIT_FAILED,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   2,
                                   0,
                                   0,
                                   0);
    this->component.doDispatch();
    ASSERT_EVENTS_RadioRecoveryScheduled_SIZE(1);
    ASSERT_EVENTS_RadioRecoveryScheduled(0,
                                         1,
                                         30,
                                         Components::RadioFault::INIT_FAILED,
                                         Components::RadioRpcResult::TARGET_ERROR);
    EXPECT_EQ(this->component.m_radioRecoveryFailures, 1U);
    EXPECT_EQ(this->component.m_radioRetryTicks, 15U);

    // An operator STATUS during backoff is observational: it may refresh the
    // factual body, but it cannot advance or restart the retry schedule.
    this->sendCmd_REQUEST_LINK_STATUS(0, 1);
    this->component.doDispatch();
    ASSERT_from_driverRequestOut_SIZE(3);
    ASSERT_from_driverRequestOut(2, Components::RadioOperation::STATUS, 0);
    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::STATUS,
                                   Components::RadioRpcResult::OK,
                                   Components::RadioState::OFF,
                                   Components::RadioFault::INIT_FAILED,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   2,
                                   0,
                                   0,
                                   0);
    this->component.doDispatch();
    ASSERT_EVENTS_RadioRecoveryScheduled_SIZE(1);
    EXPECT_EQ(this->component.m_radioRecoveryFailures, 1U);
    EXPECT_EQ(this->component.m_radioRetryTicks, 15U);

    // The first retry proves the real 2-second policy cadence: no early SET on
    // ticks 1-14, then the request is issued exactly on tick 15 (30 seconds).
    for (U32 tick = 0U; tick < 14U; tick++) {
        this->invoke_to_run(0, 0);
        this->component.doDispatch();
    }
    ASSERT_from_driverRequestOut_SIZE(3);
    EXPECT_EQ(this->component.m_radioRetryTicks, 1U);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_driverRequestOut_SIZE(4);
    ASSERT_from_driverRequestOut(3, Components::RadioOperation::SET_ENABLED, 1);
    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::SET_ENABLED,
                                   Components::RadioRpcResult::TARGET_ERROR,
                                   Components::RadioState::OFF,
                                   Components::RadioFault::INIT_FAILED,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   3,
                                   0,
                                   0,
                                   0);
    this->component.doDispatch();
    ASSERT_EVENTS_RadioRecoveryScheduled(1,
                                         2,
                                         120,
                                         Components::RadioFault::INIT_FAILED,
                                         Components::RadioRpcResult::TARGET_ERROR);
    EXPECT_EQ(this->component.m_radioRetryTicks, 60U);

    // Later rungs are accelerated to keep the unit test compact; their event
    // arguments and stored tick values verify 120 seconds and 900-second cap.
    this->component.m_radioRetryTicks = 1U;
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_driverRequestOut(4, Components::RadioOperation::SET_ENABLED, 1);
    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::SET_ENABLED,
                                   Components::RadioRpcResult::TARGET_ERROR,
                                   Components::RadioState::OFF,
                                   Components::RadioFault::INIT_FAILED,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   4,
                                   0,
                                   0,
                                   0);
    this->component.doDispatch();
    ASSERT_EVENTS_RadioRecoveryScheduled(2,
                                         3,
                                         900,
                                         Components::RadioFault::INIT_FAILED,
                                         Components::RadioRpcResult::TARGET_ERROR);
    EXPECT_EQ(this->component.m_radioRetryTicks, 450U);

    this->component.m_radioRetryTicks = 1U;
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_driverRequestOut(5, Components::RadioOperation::SET_ENABLED, 1);
    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::SET_ENABLED,
                                   Components::RadioRpcResult::TARGET_ERROR,
                                   Components::RadioState::OFF,
                                   Components::RadioFault::INIT_FAILED,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   5,
                                   0,
                                   0,
                                   0);
    this->component.doDispatch();
    ASSERT_EVENTS_RadioRecoveryScheduled(3,
                                         4,
                                         900,
                                         Components::RadioFault::INIT_FAILED,
                                         Components::RadioRpcResult::TARGET_ERROR);
    EXPECT_EQ(this->component.m_radioRetryTicks, 450U);

    this->component.m_radioRetryTicks = 1U;
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_driverRequestOut(6, Components::RadioOperation::SET_ENABLED, 1);
    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::SET_ENABLED,
                                   Components::RadioRpcResult::OK,
                                   Components::RadioState::READY,
                                   Components::RadioFault::NONE,
                                   0,
                                   0,
                                   0,
                                   0xFFFFFFFFU,
                                   6,
                                   0,
                                   0,
                                   0);
    this->component.doDispatch();
    ASSERT_EVENTS_RadioRecoveryScheduled_SIZE(4);
    ASSERT_EVENTS_RadioRecovered_SIZE(1);
    ASSERT_EVENTS_RadioRecovered(0, 4);
    EXPECT_EQ(this->component.m_radioRecoveryFailures, 0U);
    EXPECT_EQ(this->component.m_radioRetryTicks, 0U);
}

void CommsAppTester::testInitializationFailureAndInvalidReissueDoNotWedge() {
    this->clearHistory();
    Fw::String sourcePath("/tmp/missing-science.bin");
    this->invoke_to_scienceReadyIn(
        0, 9, 33, Components::ScienceProductSource::TEST, sourcePath, 0x1234);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 0);
    this->component.doDispatch();

    Fw::String emptyPath("");
    this->invoke_to_scienceReadyIn(
        0, 0, 0, Components::ScienceProductSource::UNKNOWN, emptyPath, 0);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 1);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(1, CommsAppComponentBase::OPCODE_REQUEST_SCIENCE_DOWNLINK, 1,
                        Fw::CmdResponse::BUSY);
    ASSERT_EVENTS_DownlinkRequestConflict_SIZE(1);

    // Payload reserves and publishes identity before source validation, then
    // reports the correlated initialization error under that same identity.
    this->invoke_to_payloadDownlinkStatusIn(0, 1, 7, 9, 33, 0, 1, 0);
    this->component.doDispatch();
    this->invoke_to_payloadDownlinkStatusIn(0, 4, 7, 9, 33, 0, 1, 7);
    this->component.doDispatch();
    ASSERT_EVENTS_DownlinkFailed_SIZE(1);
    ASSERT_EVENTS_DownlinkFailed(0, 4, 7);
    ASSERT_from_missionModeOut_SIZE(2);
    ASSERT_from_missionModeOut(1, Components::MissionMode::BASE, 7);
    ASSERT_TLM_DownlinkActive(1, 0);

    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 2);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(2, CommsAppComponentBase::OPCODE_REQUEST_SCIENCE_DOWNLINK, 2,
                        Fw::CmdResponse::VALIDATION_ERROR);
}

}  // namespace Components
