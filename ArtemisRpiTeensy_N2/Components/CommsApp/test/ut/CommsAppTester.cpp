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

void CommsAppTester::testDownlinkFailureReturnsBase() {
    this->clearHistory();

    Fw::String sourcePath("/tmp/science.bin");
    this->invoke_to_scienceReadyIn(0, 8, 64, Components::ScienceProductSource::TEST, sourcePath, 0x55);
    this->component.doDispatch();
    this->sendCmd_REQUEST_SCIENCE_DOWNLINK(0, 0);
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
    ASSERT_TLM_LinkState(1, 2);
}

void CommsAppTester::testFailedEnableSchedulesThirtySecondRetry() {
    this->clearHistory();

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::STATUS,
                                   Components::RadioRpcResult::OK,
                                   Components::RadioState::OFF,
                                   Components::RadioFault::NONE,
                                   0, 0, 0, 0xFFFFFFFFU, 1, 0, 0, 0);
    this->component.doDispatch();
    this->invoke_to_driverStatusIn(0,
                                   Components::RadioOperation::SET_ENABLED,
                                   Components::RadioRpcResult::TARGET_ERROR,
                                   Components::RadioState::OFF,
                                   Components::RadioFault::INIT_FAILED,
                                   0, 0, 0, 0xFFFFFFFFU, 2, 0, 0, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_RadioRecoveryScheduled_SIZE(1);
    ASSERT_EVENTS_RadioRecoveryScheduled(0,
                                         1,
                                         30,
                                         Components::RadioFault::INIT_FAILED,
                                         Components::RadioRpcResult::TARGET_ERROR);
    EXPECT_EQ(this->component.m_radioRetryTicks, 15U);

    for (U32 tick = 0U; tick < 15U; tick++) {
        this->invoke_to_run(0, 0);
        this->component.doDispatch();
    }
    ASSERT_from_driverRequestOut_SIZE(3);
    ASSERT_from_driverRequestOut(2, Components::RadioOperation::SET_ENABLED, 1);
}

}  // namespace Components
