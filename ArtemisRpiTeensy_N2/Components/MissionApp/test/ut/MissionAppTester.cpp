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

void MissionAppTester::testReportModeAnswersOnceAndChangesNothing() {
    this->clearHistory();

    // A fresh MissionApp starts in BASE. Ask it which mode it is in.
    this->sendCmd_REPORT_MODE(0, 7);
    this->component.doDispatch();

    // One reply: OK, matched to sequence number 7.
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, MissionAppComponentBase::OPCODE_REPORT_MODE, 7, Fw::CmdResponse::OK);
    // Exactly one event in total, and it is the answer: BASE.
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_ModeReport_SIZE(1);
    ASSERT_EVENTS_ModeReport(0, Components::MissionMode::BASE);
    // Read-only: no telemetry written, nothing sent to ScienceApp.
    ASSERT_TLM_SIZE(0);
    ASSERT_from_collectionRequestOut_SIZE(0);
    ASSERT_from_cancelRequestOut_SIZE(0);

    // Asking again gives the same answer and still changes nothing.
    this->sendCmd_REPORT_MODE(0, 8);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, MissionAppComponentBase::OPCODE_REPORT_MODE, 8, Fw::CmdResponse::OK);
    ASSERT_EVENTS_SIZE(2);
    ASSERT_EVENTS_ModeReport_SIZE(2);
    ASSERT_EVENTS_ModeReport(1, Components::MissionMode::BASE);
    ASSERT_EVENTS_ModeChanged_SIZE(0);
    ASSERT_TLM_SIZE(0);
    ASSERT_from_collectionRequestOut_SIZE(0);
    ASSERT_from_cancelRequestOut_SIZE(0);
}

void MissionAppTester::testReportModeTracksEveryDemoStoryMode() {
    // Walk the same demo story as testAcceptsNominalDemoStoryTransitions and ask
    // for the mode at every stop. Each stop must answer with the mode just reached.

    // BASE: where every MissionApp starts.
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(10, Components::MissionMode::BASE));

    // COLLECTION_PENDING: the ground scheduled a collection 10 s from now.
    this->sendCmd_SCHEDULE_COLLECTION(0, 11, 10);
    this->component.doDispatch();
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(12, Components::MissionMode::COLLECTION_PENDING));

    // COLLECTING: ScienceApp (port 0) says the capture started.
    this->invoke_to_modeUpdateIn(0, Components::MissionMode::COLLECTING, 10);
    this->component.doDispatch();
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(13, Components::MissionMode::COLLECTING));

    // SCIENCE_READY: ScienceApp says a science product is stored.
    this->invoke_to_modeUpdateIn(0, Components::MissionMode::SCIENCE_READY, 128);
    this->component.doDispatch();
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(14, Components::MissionMode::SCIENCE_READY));

    // DOWNLINKING: CommsApp (port 1) says the downlink started.
    this->invoke_to_modeUpdateIn(1, Components::MissionMode::DOWNLINKING, 128);
    this->component.doDispatch();
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(15, Components::MissionMode::DOWNLINKING));

    // BASE again: CommsApp says the downlink finished.
    this->invoke_to_modeUpdateIn(1, Components::MissionMode::BASE, 0);
    this->component.doDispatch();
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(16, Components::MissionMode::BASE));
}

void MissionAppTester::assertReportModeAnswers(U32 cmdSeq, Components::MissionMode expected) {
    // Forget earlier steps so the counts below only see this one REPORT_MODE.
    this->clearHistory();

    this->sendCmd_REPORT_MODE(0, cmdSeq);
    this->component.doDispatch();

    // The reply is OK and belongs to this command.
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, MissionAppComponentBase::OPCODE_REPORT_MODE, cmdSeq, Fw::CmdResponse::OK);
    // The only event is the answer, and it names the expected mode.
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_ModeReport_SIZE(1);
    ASSERT_EVENTS_ModeReport(0, expected);
    // Nothing else happened: no telemetry, no request or cancel sent to ScienceApp.
    ASSERT_TLM_SIZE(0);
    ASSERT_from_collectionRequestOut_SIZE(0);
    ASSERT_from_cancelRequestOut_SIZE(0);
}

}  // namespace Components
