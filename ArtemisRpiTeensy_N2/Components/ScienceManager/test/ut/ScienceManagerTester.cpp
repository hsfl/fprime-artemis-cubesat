#include "ScienceManagerTester.hpp"

namespace Components {

ScienceManagerTester::ScienceManagerTester()
    : ScienceManagerGTestBase("ScienceManagerTester", MAX_HISTORY_SIZE),
      component("ScienceManager") {
    this->initComponents();
    this->connectPorts();
}

ScienceManagerTester::~ScienceManagerTester() {
    this->component.deinit();
}

void ScienceManagerTester::testDefaultDurationIsThirty() {
    this->clearHistory();

    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 30);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 30);
    ASSERT_TLM_CaptureDurationSeconds(0, 30);
}

void ScienceManagerTester::testScheduledCountdownFiresExactlyOnce() {
    this->clearHistory();

    this->invoke_to_requestIn(0, 2);
    this->component.doDispatch();

    ASSERT_EVENTS_CollectionTriggered_SIZE(1);
    ASSERT_EVENTS_CollectionTriggered(0, 2);
    ASSERT_TLM_PendingDelaySeconds(0, 2);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_TLM_PendingDelaySeconds(1, 1);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 30);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 30);
    ASSERT_TLM_PendingDelaySeconds(2, 0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_missionModeOut_SIZE(1);
}

void ScienceManagerTester::testRejectsZeroDelayAndBadDurations() {
    this->clearHistory();

    this->invoke_to_requestIn(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(1);
    ASSERT_EVENTS_ScienceCommandRejected(0, 1, 0);
    ASSERT_TLM_PendingDelaySeconds_SIZE(0);

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceManagerComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 0,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(2);
    ASSERT_EVENTS_ScienceCommandRejected(1, 2, 0);

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 1, 121);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, ScienceManagerComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 1,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(3);
    ASSERT_EVENTS_ScienceCommandRejected(2, 2, 121);

    this->sendCmd_SCIENCE_CAPTURE(0, 2, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(3);
    ASSERT_CMD_RESPONSE(2, ScienceManagerComponentBase::OPCODE_SCIENCE_CAPTURE, 2,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(4);
    ASSERT_EVENTS_ScienceCommandRejected(3, 2, 0);
    ASSERT_from_payloadRequestOut_SIZE(0);

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 3, 120);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(4);
    ASSERT_CMD_RESPONSE(3, ScienceManagerComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 3,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_CaptureDurationConfigured_SIZE(1);
    ASSERT_EVENTS_CaptureDurationConfigured(0, 120);
}

void ScienceManagerTester::testCancelClearsCountdown() {
    this->clearHistory();

    this->invoke_to_requestIn(0, 2);
    this->component.doDispatch();
    this->invoke_to_cancelRequestIn(0, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_CollectionCancelled_SIZE(1);
    ASSERT_EVENTS_CollectionCancelled(0, 2);
    ASSERT_TLM_PendingDelaySeconds(1, 0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
}

void ScienceManagerTester::testQueuedCancelAtFinalTickBoundaryPreventsCollection() {
    this->clearHistory();

    this->invoke_to_requestIn(0, 2);
    this->component.doDispatch();

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_TLM_PendingDelaySeconds(1, 1);
    ASSERT_from_payloadRequestOut_SIZE(0);

    this->invoke_to_cancelRequestIn(0, 0);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_CollectionCancelled_SIZE(1);
    ASSERT_EVENTS_CollectionCancelled(0, 1);
    ASSERT_TLM_PendingDelaySeconds(2, 0);

    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
}

void ScienceManagerTester::testScienceCaptureDoesNotPersistDuration() {
    this->clearHistory();

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 0, 45);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_TLM_CaptureDurationSeconds(0, 45);

    this->clearHistory();
    this->sendCmd_SCIENCE_CAPTURE(0, 1, 5);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceManagerComponentBase::OPCODE_SCIENCE_CAPTURE, 1,
                        Fw::CmdResponse::OK);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 5);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 5);
    ASSERT_TLM_CaptureDurationSeconds_SIZE(0);
    ASSERT_EVENTS_CaptureDurationConfigured_SIZE(0);

    this->clearHistory();
    this->sendCmd_START_COLLECTION(0, 2);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 45);
    ASSERT_TLM_CaptureDurationSeconds_SIZE(0);
}

void ScienceManagerTester::testParamSeedsDurationAtInit() {
    this->paramSet_CAPTURE_DURATION_SECONDS(45, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->component.preamble();

    ASSERT_EVENTS_ScienceCommandRejected_SIZE(0);
    ASSERT_TLM_CaptureDurationSeconds(0, 45);

    this->clearHistory();
    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceManagerComponentBase::OPCODE_START_COLLECTION, 0,
                        Fw::CmdResponse::OK);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 45);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 45);
}

void ScienceManagerTester::testInvalidParamFallsBackToThirty() {
    this->paramSet_CAPTURE_DURATION_SECONDS(121, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->component.preamble();

    ASSERT_EVENTS_ScienceCommandRejected_SIZE(1);
    ASSERT_EVENTS_ScienceCommandRejected(0, 2, 121);
    ASSERT_TLM_CaptureDurationSeconds(0, 30);

    this->clearHistory();
    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceManagerComponentBase::OPCODE_START_COLLECTION, 0,
                        Fw::CmdResponse::OK);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 30);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 30);
}

}  // namespace Components
