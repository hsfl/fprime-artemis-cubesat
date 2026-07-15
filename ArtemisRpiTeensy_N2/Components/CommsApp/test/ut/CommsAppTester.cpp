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
    ASSERT_from_downlinkRequestOut_SIZE(0);
    ASSERT_from_payloadDownlinkRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
}

void CommsAppTester::testRequestsScienceDownlinkAndCompletionClearsState() {
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
    ASSERT_from_downlinkRequestOut_SIZE(1);
    ASSERT_from_downlinkRequestOut(0, 7, 128, Components::ScienceProductSource::NEUTRON_SIM, sourcePath, 0x1234);
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
                        Fw::CmdResponse::VALIDATION_ERROR);
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
    ASSERT_from_downlinkRequestOut_SIZE(1);
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
    ASSERT_from_downlinkRequestOut_SIZE(1);
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
    ASSERT_from_downlinkRequestOut_SIZE(2);
    ASSERT_from_downlinkRequestOut(1, 8, 256, Components::ScienceProductSource::TEST, nextPath, 0x5678);
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
    ASSERT_from_driverRequestOut(0, 1);

    this->invoke_to_driverStatusIn(0, 2);
    this->component.doDispatch();
    ASSERT_EVENTS_LinkStateUpdated_SIZE(1);
    ASSERT_EVENTS_LinkStateUpdated(0, 2, -120);

    this->invoke_to_rssiStatusIn(0, -72);
    this->component.doDispatch();
    this->sendCmd_PING_LINK_RSSI(0, 1);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_from_driverRequestOut_SIZE(2);
    ASSERT_from_driverRequestOut(1, 3);

    this->invoke_to_driverStatusIn(0, 3);
    this->component.doDispatch();
    ASSERT_EVENTS_LinkStateUpdated_SIZE(2);
    ASSERT_EVENTS_LinkStateUpdated(1, 3, -72);
    ASSERT_EVENTS_LinkRssiPing_SIZE(1);
    ASSERT_EVENTS_LinkRssiPing(0, 3, -72, 2);
}

void CommsAppTester::testRunPublishesHealthFromLinkState() {
    this->clearHistory();

    this->invoke_to_linkStatusIn(0, 0);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_sohStatusOut_SIZE(1);
    ASSERT_from_sohStatusOut(0, Components::HealthState::FAIL, 0);
    ASSERT_from_driverRequestOut_SIZE(1);
    ASSERT_from_driverRequestOut(0, 1);

    this->invoke_to_linkStatusIn(0, 1);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_sohStatusOut_SIZE(2);
    ASSERT_from_sohStatusOut(1, Components::HealthState::WARN, 1);

    this->invoke_to_linkStatusIn(0, 2);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_sohStatusOut_SIZE(3);
    ASSERT_from_sohStatusOut(2, Components::HealthState::OK, 2);
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
