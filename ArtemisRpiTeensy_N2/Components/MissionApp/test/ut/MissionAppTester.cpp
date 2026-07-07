#include "MissionAppTester.hpp"

namespace Components {

MissionAppTester::MissionAppTester()
    : MissionAppGTestBase("MissionAppTester", MAX_HISTORY_SIZE),
      component("MissionApp") {
    this->initComponents();
    this->connectPorts();
}

MissionAppTester::~MissionAppTester() {
    this->component.deinit();
}

void MissionAppTester::testRejectsInvalidManagerTransition() {
    this->clearHistory();

    this->invoke_to_modeUpdateIn(0, Components::MissionMode::SCIENCE_READY, 42);
    this->component.doDispatch();

    ASSERT_EVENTS_ModeUpdateRejected_SIZE(1);
    ASSERT_EVENTS_ModeUpdateRejected(
        0,
        Components::MissionMode::SCIENCE_READY,
        Components::MissionMode::BASE,
        42);
    ASSERT_EVENTS_ModeChanged_SIZE(0);

    for (U32 tick = 0; tick < 30; ++tick) {
        this->invoke_to_run(0, 0);
        this->component.doDispatch();
    }
    ASSERT_TLM_CurrentMode(0, Components::MissionMode::BASE);
}

void MissionAppTester::testRejectsInvalidScheduleCommandInputs() {
    this->clearHistory();

    this->sendCmd_SCHEDULE_COLLECTION(0, 0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, MissionAppComponentBase::OPCODE_SCHEDULE_COLLECTION, 0,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_MissionCommandRejected_SIZE(1);
    ASSERT_EVENTS_MissionCommandRejected(0, 1, 0);
    ASSERT_from_collectionRequestOut_SIZE(0);

    this->sendCmd_SCHEDULE_COLLECTION(0, 1, 301);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, MissionAppComponentBase::OPCODE_SCHEDULE_COLLECTION, 1,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_MissionCommandRejected_SIZE(2);
    ASSERT_EVENTS_MissionCommandRejected(1, 1, 301);
    ASSERT_from_collectionRequestOut_SIZE(0);
}

void MissionAppTester::testRejectsInvalidCommandTransition() {
    this->clearHistory();

    this->sendCmd_SCHEDULE_COLLECTION(0, 0, 10);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(0, Components::MissionMode::COLLECTING, 10);
    this->component.doDispatch();

    this->sendCmd_SCHEDULE_COLLECTION(0, 1, 20);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, MissionAppComponentBase::OPCODE_SCHEDULE_COLLECTION, 1,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ModeUpdateRejected_SIZE(1);
    ASSERT_EVENTS_ModeUpdateRejected(0, Components::MissionMode::COLLECTION_PENDING,
                                     Components::MissionMode::COLLECTING, 20);
    ASSERT_EVENTS_MissionCommandRejected_SIZE(1);
    ASSERT_EVENTS_MissionCommandRejected(0, 2, 20);
    ASSERT_from_collectionRequestOut_SIZE(1);
    ASSERT_from_collectionRequestOut(0, 10);
}

void MissionAppTester::testAcceptsNominalDemoStoryTransitions() {
    this->clearHistory();

    this->sendCmd_SCHEDULE_COLLECTION(0, 0, 10);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_ModeChanged_SIZE(1);
    ASSERT_EVENTS_ModeChanged(0, Components::MissionMode::COLLECTION_PENDING);
    ASSERT_EVENTS_CollectionScheduled_SIZE(1);
    ASSERT_EVENTS_CollectionScheduled(0, 10);
    ASSERT_from_collectionRequestOut_SIZE(1);
    ASSERT_from_collectionRequestOut(0, 10);

    this->invoke_to_modeUpdateIn(0, Components::MissionMode::COLLECTING, 10);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(0, Components::MissionMode::SCIENCE_READY, 128);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Components::MissionMode::DOWNLINKING, 128);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Components::MissionMode::BASE, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_ModeUpdateRejected_SIZE(0);
    ASSERT_EVENTS_ModeChanged_SIZE(5);
    ASSERT_EVENTS_ModeChanged(1, Components::MissionMode::COLLECTING);
    ASSERT_EVENTS_ModeChanged(2, Components::MissionMode::SCIENCE_READY);
    ASSERT_EVENTS_ModeChanged(3, Components::MissionMode::DOWNLINKING);
    ASSERT_EVENTS_ModeChanged(4, Components::MissionMode::BASE);

    ASSERT_TLM_CurrentMode(0, Components::MissionMode::COLLECTION_PENDING);
    ASSERT_TLM_CurrentMode(4, Components::MissionMode::BASE);
    ASSERT_TLM_LastScheduledDelaySeconds(0, 10);
}

void MissionAppTester::testAcceptsManualDownlinkRetryFromBase() {
    this->clearHistory();

    this->invoke_to_modeUpdateIn(1, Components::MissionMode::DOWNLINKING, 128);
    this->component.doDispatch();

    ASSERT_EVENTS_ModeUpdateRejected_SIZE(0);
    ASSERT_EVENTS_ModeChanged_SIZE(1);
    ASSERT_EVENTS_ModeChanged(0, Components::MissionMode::DOWNLINKING);

    this->invoke_to_modeUpdateIn(1, Components::MissionMode::BASE, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_ModeChanged_SIZE(2);
    ASSERT_EVENTS_ModeChanged(1, Components::MissionMode::BASE);
}

void MissionAppTester::testEnterBaseModeCancelsPendingCollection() {
    this->clearHistory();

    this->sendCmd_SCHEDULE_COLLECTION(0, 0, 10);
    this->component.doDispatch();
    this->sendCmd_ENTER_BASE_MODE(0, 1);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, MissionAppComponentBase::OPCODE_ENTER_BASE_MODE, 1,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_ModeChanged_SIZE(2);
    ASSERT_EVENTS_ModeChanged(0, Components::MissionMode::COLLECTION_PENDING);
    ASSERT_EVENTS_ModeChanged(1, Components::MissionMode::BASE);
    ASSERT_from_cancelRequestOut_SIZE(1);
    ASSERT_from_cancelRequestOut(0, 0);
    ASSERT_TLM_CurrentMode(1, Components::MissionMode::BASE);
    ASSERT_TLM_LastScheduledDelaySeconds(0, 10);
    ASSERT_TLM_LastScheduledDelaySeconds(1, 0);
}

void MissionAppTester::testCancelCollectionReturnsBaseAndCancels() {
    this->clearHistory();

    this->sendCmd_SCHEDULE_COLLECTION(0, 0, 15);
    this->component.doDispatch();
    this->sendCmd_CANCEL_COLLECTION(0, 1);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, MissionAppComponentBase::OPCODE_CANCEL_COLLECTION, 1,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_ModeChanged_SIZE(2);
    ASSERT_EVENTS_ModeChanged(1, Components::MissionMode::BASE);
    ASSERT_from_cancelRequestOut_SIZE(1);
    ASSERT_from_cancelRequestOut(0, 0);
    ASSERT_TLM_CurrentMode(1, Components::MissionMode::BASE);
    ASSERT_TLM_LastScheduledDelaySeconds(1, 0);
}

}  // namespace Components
