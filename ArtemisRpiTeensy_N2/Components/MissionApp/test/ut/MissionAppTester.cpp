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

// ----------------------------------------------------------------------
// D5 demo modes
//
// The tables below are the specification, written out by hand from the
// design (D5 Demo Modes report, transition table). They do not reuse the
// component's own logic, so a slip in MissionApp.cpp shows up as a failed cell.
// ----------------------------------------------------------------------

namespace {

using Mode = Components::MissionMode;
using Ops = Components::OperationsMode;
using Reason = Components::ModeRejectReason;
using Request = Components::SubsystemRequest;

constexpr U8 MODE_COUNT = 6;  // BASE .. SAFE
constexpr U8 OPS_COUNT = 5;   // BASE .. SAFE

Mode modeAt(U8 index) {
    return static_cast<Mode::T>(index);
}

Ops opsAt(U8 index) {
    return static_cast<Ops::T>(index);
}

// What SET_MODE aims for, in demo-step terms.
Mode missionModeFor(Ops target) {
    switch (target) {
        case Ops::SCIENCE_COLLECTION:
            return Mode::COLLECTING;
        case Ops::SCIENCE_PROCESSING:
            return Mode::SCIENCE_READY;
        case Ops::SCIENCE_DOWNLINK:
            return Mode::DOWNLINKING;
        case Ops::SAFE:
            return Mode::SAFE;
        default:
            return Mode::BASE;
    }
}

// Subsystem reports (ScienceApp, CommsApp). Rows: current mode. Columns: requested mode.
// Order for both: BASE, COLLECTION_PENDING, COLLECTING, SCIENCE_READY, DOWNLINKING, SAFE.
// true = accepted (ModeChanged). false = refused (ModeUpdateRejected), mode stays.
constexpr bool SUBSYSTEM_ALLOWED[MODE_COUNT][MODE_COUNT] = {
    /* BASE               */ {true, true, true, false, true, false},
    /* COLLECTION_PENDING */ {true, true, true, false, false, false},
    /* COLLECTING         */ {true, false, true, true, false, false},
    /* SCIENCE_READY      */ {true, false, false, true, true, false},
    /* DOWNLINKING        */ {true, false, false, false, true, false},
    /* SAFE               */ {false, false, false, false, false, false},
};

// SET_MODE from the ground. Rows: current mode. Columns: requested operations mode,
// in the order BASE, SCIENCE_COLLECTION, SCIENCE_PROCESSING, SCIENCE_DOWNLINK, SAFE.
constexpr Reason::T SET_MODE_RESULT[MODE_COUNT][OPS_COUNT] = {
    /* BASE               */ {Reason::ALLOWED, Reason::USE_SCHEDULE_COLLECTION, Reason::AUTOMATIC_ONLY, Reason::ALLOWED, Reason::ALLOWED},
    /* COLLECTION_PENDING */ {Reason::ALLOWED, Reason::USE_SCHEDULE_COLLECTION, Reason::AUTOMATIC_ONLY, Reason::COLLECTION_ACTIVE, Reason::ALLOWED},
    /* COLLECTING         */ {Reason::ALLOWED, Reason::ALLOWED, Reason::AUTOMATIC_ONLY, Reason::COLLECTION_ACTIVE, Reason::ALLOWED},
    /* SCIENCE_READY      */ {Reason::ALLOWED, Reason::USE_SCHEDULE_COLLECTION, Reason::ALLOWED, Reason::ALLOWED, Reason::ALLOWED},
    /* DOWNLINKING        */ {Reason::ALLOWED, Reason::USE_SCHEDULE_COLLECTION, Reason::AUTOMATIC_ONLY, Reason::ALLOWED, Reason::ALLOWED},
    /* SAFE               */ {Reason::ALLOWED, Reason::SAFE_LOCKED, Reason::SAFE_LOCKED, Reason::SAFE_LOCKED, Reason::ALLOWED},
};

// MOP v2.0 §8 "Hardware Status", copied by hand for the five demo modes.
struct MopRow {
    Ops::T mode;
    Request::T eps;
    Request::T obc;
    Request::T uhfTx;
    Request::T uhfRx;
    Request::T sband;
    Request::T gps;
    Request::T adcs;
    Request::T payload;
};

constexpr MopRow MOP_BASE = {Ops::BASE, Request::ON, Request::ON, Request::TBC, Request::ON,
                             Request::OFF, Request::ON, Request::FLAG_DEPENDENT, Request::OFF};
constexpr MopRow MOP_COLLECTION = {Ops::SCIENCE_COLLECTION, Request::ON, Request::ON, Request::TBC, Request::ON,
                                   Request::OFF, Request::ON, Request::LVLH, Request::ACQUISITION};
constexpr MopRow MOP_PROCESSING = {Ops::SCIENCE_PROCESSING, Request::ON, Request::ON, Request::TBC, Request::ON,
                                   Request::OFF, Request::ON, Request::SUN_TRACKING, Request::STANDBY};
constexpr MopRow MOP_DOWNLINK = {Ops::SCIENCE_DOWNLINK, Request::ON, Request::ON, Request::ON, Request::ON,
                                 Request::ON, Request::ON, Request::TARGET_TRACKING, Request::OFF};
constexpr MopRow MOP_SAFE = {Ops::SAFE, Request::MINIMAL, Request::MINIMAL, Request::TBC, Request::ON,
                             Request::OFF, Request::OFF, Request::OFF, Request::OFF};

}  // namespace

void MissionAppTester::finishSafeEntry() {
    this->invoke_to_captureControlReportIn(0, Fw::Enabled::DISABLED, false);
    this->component.doDispatch();
    this->invoke_to_cancelReportIn(0, MissionApp::SAFE_CANCEL_KEY);
    this->component.doDispatch();
}

void MissionAppTester::runTicks(U32 ticks) {
    for (U32 tick = 0; tick < ticks; ++tick) {
        this->invoke_to_run(0, 0);
        this->component.doDispatch();
    }
}

void MissionAppTester::sendSetMode(U32 cmdSeq, Components::OperationsMode target, Fw::CmdResponse expected) {
    const FwSizeType before = this->cmdResponseHistory->size();
    this->sendCmd_SET_MODE(0, cmdSeq, target);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(before + 1);
    ASSERT_CMD_RESPONSE(before, MissionAppComponentBase::OPCODE_SET_MODE, cmdSeq, expected);
}

void MissionAppTester::driveTo(Components::MissionMode mode) {
    if (mode == Mode::SAFE) {
        // Ground-commanded SAFE: no fault, no latch, so the exit guard is open.
        this->sendCmd_SET_MODE(0, 900, Ops::SAFE);
        this->component.doDispatch();
        this->finishSafeEntry();
    } else if (mode != Mode::BASE) {
        // The demo chain in order, stopping at the requested step.
        this->sendCmd_SCHEDULE_COLLECTION(0, 901, 10);
        this->component.doDispatch();
        if (mode != Mode::COLLECTION_PENDING) {
            this->invoke_to_modeUpdateIn(0, Mode::COLLECTING, 10);
            this->component.doDispatch();
        }
        if ((mode == Mode::SCIENCE_READY) || (mode == Mode::DOWNLINKING)) {
            this->invoke_to_modeUpdateIn(0, Mode::SCIENCE_READY, 128);
            this->component.doDispatch();
        }
        if (mode == Mode::DOWNLINKING) {
            this->invoke_to_modeUpdateIn(1, Mode::DOWNLINKING, 128);
            this->component.doDispatch();
        }
    }
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(999, mode));
    this->clearHistory();
}

void MissionAppTester::checkSubsystemReport(Components::MissionMode from,
                                            Components::MissionMode requested,
                                            bool allowed) {
    ASSERT_NO_FATAL_FAILURE(this->driveTo(from));

    this->invoke_to_modeUpdateIn(0, requested, 77);
    this->component.doDispatch();

    if (allowed) {
        ASSERT_EVENTS_ModeChanged_SIZE(1);
        ASSERT_EVENTS_ModeChanged(0, requested);
        ASSERT_EVENTS_ModeUpdateRejected_SIZE(0);
    } else {
        ASSERT_EVENTS_ModeChanged_SIZE(0);
        ASSERT_EVENTS_ModeUpdateRejected_SIZE(1);
        ASSERT_EVENTS_ModeUpdateRejected(0, requested, from, 77);
    }
    // A subsystem report never cancels a collection and never enters SAFE.
    ASSERT_from_cancelRequestOut_SIZE(0);
    ASSERT_EVENTS_SafeModeEntered_SIZE(0);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(1000, allowed ? requested : from));
}

void MissionAppTester::checkSetMode(Components::MissionMode from,
                                    Components::OperationsMode target,
                                    Components::ModeRejectReason expected) {
    ASSERT_NO_FATAL_FAILURE(this->driveTo(from));

    this->sendCmd_SET_MODE(0, 50, target);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    if (expected == Reason::ALLOWED) {
        const Mode reached = missionModeFor(target);
        const bool entersSafe = (reached == Mode::SAFE) && (from != Mode::SAFE);
        if (entersSafe) {
            this->finishSafeEntry();
        }
        ASSERT_CMD_RESPONSE(0, MissionAppComponentBase::OPCODE_SET_MODE, 50, Fw::CmdResponse::OK);
        ASSERT_EVENTS_SetModeRejected_SIZE(0);
        ASSERT_EVENTS_SafeExitBlocked_SIZE(0);
        ASSERT_EVENTS_ModeChanged_SIZE(1);
        ASSERT_EVENTS_ModeChanged(0, reached);
        ASSERT_EVENTS_SafeModeEntered_SIZE(entersSafe ? 1 : 0);
        if (entersSafe) {
            ASSERT_EVENTS_SafeModeEntered(0, Components::SafeCause::GROUND_COMMAND, 0);
        }
        // BASE and a fresh SAFE entry both drop any queued collection.
        ASSERT_from_cancelRequestOut_SIZE(((reached == Mode::BASE) || entersSafe) ? 1 : 0);
        ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(51, reached));
    } else {
        ASSERT_CMD_RESPONSE(0, MissionAppComponentBase::OPCODE_SET_MODE, 50, Fw::CmdResponse::VALIDATION_ERROR);
        ASSERT_EVENTS_ModeChanged_SIZE(0);
        ASSERT_EVENTS_SetModeRejected_SIZE(1);
        ASSERT_EVENTS_SetModeRejected(0, target, from, expected);
        ASSERT_from_cancelRequestOut_SIZE(0);
        ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(51, from));
    }
}

void MissionAppTester::checkFault(Components::MissionMode from) {
    ASSERT_NO_FATAL_FAILURE(this->driveTo(from));

    this->invoke_to_faultIn(0, Components::SafeCause::LOW_POWER, false, 3);
    this->component.doDispatch();

    if (from == Mode::SAFE) {
        // Already there: no new entry, but the fault is reported and restarts the timer.
        ASSERT_EVENTS_ModeChanged_SIZE(0);
        ASSERT_EVENTS_SafeModeEntered_SIZE(0);
        ASSERT_EVENTS_SafeFaultRepeated_SIZE(1);
        ASSERT_EVENTS_SafeFaultRepeated(0, Components::SafeCause::LOW_POWER, 0);
        ASSERT_from_cancelRequestOut_SIZE(0);
    } else {
        this->finishSafeEntry();
        ASSERT_EVENTS_ModeChanged_SIZE(1);
        ASSERT_EVENTS_ModeChanged(0, Mode::SAFE);
        ASSERT_EVENTS_SafeModeEntered_SIZE(1);
        ASSERT_EVENTS_SafeModeEntered(0, Components::SafeCause::LOW_POWER, 0);
        ASSERT_from_cancelRequestOut_SIZE(1);
    }
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(1001, Mode::SAFE));
}

void MissionAppTester::testTransitionTableSubsystemReports() {
    // 6 x 6 = 36 cells, each on a fresh MissionApp.
    for (U8 from = 0; from < MODE_COUNT; ++from) {
        for (U8 requested = 0; requested < MODE_COUNT; ++requested) {
            SCOPED_TRACE(::testing::Message() << "subsystem report: from=" << static_cast<int>(from)
                                              << " requested=" << static_cast<int>(requested));
            MissionAppTester tester;
            ASSERT_NO_FATAL_FAILURE(
                tester.checkSubsystemReport(modeAt(from), modeAt(requested), SUBSYSTEM_ALLOWED[from][requested]));
        }
    }
}

void MissionAppTester::testTransitionTableGroundSetMode() {
    // 6 x 5 = 30 cells, each on a fresh MissionApp.
    for (U8 from = 0; from < MODE_COUNT; ++from) {
        for (U8 target = 0; target < OPS_COUNT; ++target) {
            SCOPED_TRACE(::testing::Message() << "SET_MODE: from=" << static_cast<int>(from)
                                              << " target=" << static_cast<int>(target));
            MissionAppTester tester;
            ASSERT_NO_FATAL_FAILURE(tester.checkSetMode(modeAt(from), opsAt(target), SET_MODE_RESULT[from][target]));
        }
    }
}

void MissionAppTester::testTransitionTableFaults() {
    // A fault reaches SAFE from every mode.
    for (U8 from = 0; from < MODE_COUNT; ++from) {
        SCOPED_TRACE(::testing::Message() << "fault: from=" << static_cast<int>(from));
        MissionAppTester tester;
        ASSERT_NO_FATAL_FAILURE(tester.checkFault(modeAt(from)));
    }
}

void MissionAppTester::testSafeExitBlockedByLatchUntilCleared() {
    this->clearHistory();

    // A latching watchdog fault puts the spacecraft in SAFE and holds it there.
    this->invoke_to_faultIn(0, Components::SafeCause::WATCHDOG, true, 0);
    this->component.doDispatch();
    this->finishSafeEntry();
    ASSERT_EVENTS_SafeModeEntered_SIZE(1);
    ASSERT_EVENTS_SafeModeEntered(0, Components::SafeCause::WATCHDOG, 1);
    ASSERT_TLM_SafeLatched(0, true);
    ASSERT_TLM_SafeEntryCause(0, Components::SafeCause::WATCHDOG);

    // Well past the 10 s stability interval, the latch alone still blocks both exit commands.
    this->runTicks(15);
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(1, Ops::BASE, Fw::CmdResponse::VALIDATION_ERROR));
    ASSERT_EVENTS_SafeExitBlocked_SIZE(1);
    ASSERT_EVENTS_SafeExitBlocked(0, Components::SafeCause::WATCHDOG, 15, 10);
    ASSERT_EVENTS_SetModeRejected_SIZE(0);

    this->sendCmd_ENTER_BASE_MODE(0, 2);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(1, MissionAppComponentBase::OPCODE_ENTER_BASE_MODE, 2, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_SafeExitBlocked_SIZE(2);
    ASSERT_EVENTS_MissionCommandRejected_SIZE(1);
    ASSERT_EVENTS_MissionCommandRejected(0, 2, 0);
    ASSERT_EVENTS_ModeChanged_SIZE(0);

    // The ground clears the latch. That alone does not leave SAFE.
    this->sendCmd_CLEAR_SAFE_LATCH(0, 3);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(2, MissionAppComponentBase::OPCODE_CLEAR_SAFE_LATCH, 3, Fw::CmdResponse::OK);
    ASSERT_EVENTS_SafeLatchCleared_SIZE(1);
    ASSERT_EVENTS_SafeLatchCleared(0, Components::SafeCause::WATCHDOG);
    ASSERT_TLM_SafeLatched(0, false);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(4, Mode::SAFE));

    // Now the exit passes: back to BASE, cause cleared.
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(5, Ops::BASE, Fw::CmdResponse::OK));
    ASSERT_EVENTS_ModeChanged_SIZE(1);
    ASSERT_EVENTS_ModeChanged(0, Mode::BASE);
    ASSERT_TLM_SafeEntryCause(0, Components::SafeCause::NONE);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(6, Mode::BASE));
}

void MissionAppTester::testSafeExitWaitsForStabilityInterval() {
    this->clearHistory();

    this->invoke_to_faultIn(0, Components::SafeCause::LOW_POWER, false, 0);
    this->component.doDispatch();
    this->finishSafeEntry();

    // 9 of 10 seconds: too early.
    this->runTicks(9);
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(1, Ops::BASE, Fw::CmdResponse::VALIDATION_ERROR));
    ASSERT_EVENTS_SafeExitBlocked(0, Components::SafeCause::NONE, 9, 10);

    // A second fault restarts the clock.
    this->invoke_to_faultIn(0, Components::SafeCause::LOW_POWER, false, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_SafeFaultRepeated_SIZE(1);
    this->runTicks(9);
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(2, Ops::BASE, Fw::CmdResponse::VALIDATION_ERROR));
    ASSERT_EVENTS_SafeExitBlocked(1, Components::SafeCause::NONE, 9, 10);

    // One more second: 10 of 10, the exit passes.
    this->runTicks(1);
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(3, Ops::BASE, Fw::CmdResponse::OK));
    ASSERT_EVENTS_ModeChanged(0, Mode::BASE);
}

void MissionAppTester::testSafeExitIntervalUsesParameter() {
    this->paramSet_SAFE_EXIT_STABLE_SECONDS(3, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->clearHistory();

    this->invoke_to_faultIn(0, Components::SafeCause::ADCS_RATE, false, 0);
    this->component.doDispatch();
    this->finishSafeEntry();

    this->runTicks(2);
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(1, Ops::BASE, Fw::CmdResponse::VALIDATION_ERROR));
    ASSERT_EVENTS_SafeExitBlocked(0, Components::SafeCause::NONE, 2, 3);

    this->runTicks(1);
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(2, Ops::BASE, Fw::CmdResponse::OK));
    ASSERT_EVENTS_ModeChanged(0, Mode::BASE);
}

void MissionAppTester::testCaptureFinishingInSafeDoesNotLeaveSafe() {
    // A capture is running when a low-power fault arrives.
    ASSERT_NO_FATAL_FAILURE(this->driveTo(Mode::COLLECTING));
    this->invoke_to_faultIn(0, Components::SafeCause::LOW_POWER, false, 0);
    this->component.doDispatch();
    this->finishSafeEntry();
    ASSERT_EVENTS_ModeChanged(0, Mode::SAFE);
    this->clearHistory();

    // The payload finishes anyway and ScienceApp reports the stored product, then CommsApp
    // reports a downlink ending. SAFE holds against every one of them.
    this->invoke_to_modeUpdateIn(0, Mode::SCIENCE_READY, 128);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Mode::DOWNLINKING, 128);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Mode::BASE, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_ModeChanged_SIZE(0);
    ASSERT_EVENTS_ModeUpdateRejected_SIZE(3);
    ASSERT_EVENTS_ModeUpdateRejected(0, Mode::SCIENCE_READY, Mode::SAFE, 128);
    ASSERT_EVENTS_ModeUpdateRejected(1, Mode::DOWNLINKING, Mode::SAFE, 128);
    ASSERT_EVENTS_ModeUpdateRejected(2, Mode::BASE, Mode::SAFE, 0);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(7, Mode::SAFE));
}

void MissionAppTester::testOldCommandsRespectSafe() {
    ASSERT_NO_FATAL_FAILURE(this->driveTo(Mode::SAFE));

    // SCHEDULE_COLLECTION is refused in SAFE and nothing reaches ScienceApp.
    this->sendCmd_SCHEDULE_COLLECTION(0, 1, 10);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(0, MissionAppComponentBase::OPCODE_SCHEDULE_COLLECTION, 1, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ModeUpdateRejected(0, Mode::COLLECTION_PENDING, Mode::SAFE, 10);
    ASSERT_EVENTS_MissionCommandRejected(0, 2, 10);
    ASSERT_from_collectionRequestOut_SIZE(0);

    // CANCEL_COLLECTION does not double as a Safe exit.
    this->sendCmd_CANCEL_COLLECTION(0, 2);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(1, MissionAppComponentBase::OPCODE_CANCEL_COLLECTION, 2, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ModeUpdateRejected(1, Mode::BASE, Mode::SAFE, 0);
    ASSERT_EVENTS_MissionCommandRejected(1, 2, 0);
    ASSERT_from_cancelRequestOut_SIZE(0);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(3, Mode::SAFE));

    // ENTER_BASE_MODE is a Safe exit request. Ground-commanded SAFE has no fault, so it passes.
    this->clearHistory();
    this->sendCmd_ENTER_BASE_MODE(0, 4);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(0, MissionAppComponentBase::OPCODE_ENTER_BASE_MODE, 4, Fw::CmdResponse::OK);
    ASSERT_EVENTS_ModeChanged(0, Mode::BASE);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(5, Mode::BASE));
}

void MissionAppTester::testSafeEntryCancelsQueuedCollection() {
    this->clearHistory();
    this->sendCmd_SCHEDULE_COLLECTION(0, 1, 30);
    this->component.doDispatch();

    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(2, Ops::SAFE, Fw::CmdResponse::OK));
    this->finishSafeEntry();
    ASSERT_EVENTS_ModeChanged_SIZE(2);
    ASSERT_EVENTS_ModeChanged(1, Mode::SAFE);
    ASSERT_EVENTS_SafeModeEntered(0, Components::SafeCause::GROUND_COMMAND, 0);
    ASSERT_from_cancelRequestOut_SIZE(1);
    ASSERT_TLM_LastScheduledDelaySeconds(0, 30);
    ASSERT_TLM_LastScheduledDelaySeconds(1, 0);
    ASSERT_TLM_SafeEntryCause(1, Components::SafeCause::GROUND_COMMAND);
}

// SAFE entry is complete only after PayloadManager and ScienceApp both report back.
void MissionAppTester::testSafeEntryWaitsForBothReports() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(1, Ops::SAFE, Fw::CmdResponse::OK));
    ASSERT_EVENTS_ModeChanged(0, Mode::SAFE);
    ASSERT_from_captureControlOut_SIZE(1);
    ASSERT_from_captureControlOut(0, Fw::Enabled::DISABLED);
    ASSERT_from_cancelRequestOut_SIZE(1);
    const U32 safeKey = MissionApp::SAFE_CANCEL_KEY;
    ASSERT_from_cancelRequestOut(0, safeKey);
    ASSERT_EVENTS_SafeModeEntered_SIZE(0);

    this->invoke_to_captureControlReportIn(0, Fw::Enabled::DISABLED, true);
    this->component.doDispatch();
    ASSERT_EVENTS_SafeModeEntered_SIZE(0);

    this->invoke_to_cancelReportIn(0, safeKey);
    this->component.doDispatch();
    ASSERT_EVENTS_SafeModeEntered_SIZE(1);
    ASSERT_EVENTS_SafeModeEntered(0, Components::SafeCause::GROUND_COMMAND, 0);
}

// A cancel sent before SAFE can still be reporting back. Only SAFE's own cancel counts.
void MissionAppTester::testStaleCancelReportCannotConfirmSafe() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(1, Ops::SAFE, Fw::CmdResponse::OK));
    this->invoke_to_captureControlReportIn(0, Fw::Enabled::DISABLED, false);
    this->component.doDispatch();
    this->invoke_to_cancelReportIn(0, 0U);  // the report of an earlier ENTER_BASE_MODE
    this->component.doDispatch();
    ASSERT_EVENTS_SafeModeEntered_SIZE(0);

    this->invoke_to_cancelReportIn(0, MissionApp::SAFE_CANCEL_KEY);
    this->component.doDispatch();
    ASSERT_EVENTS_SafeModeEntered_SIZE(1);
}

// An ENABLED report, from leaving an earlier SAFE, says nothing about this inhibit.
void MissionAppTester::testEnabledReportCannotConfirmSafe() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(1, Ops::SAFE, Fw::CmdResponse::OK));
    this->invoke_to_cancelReportIn(0, MissionApp::SAFE_CANCEL_KEY);
    this->component.doDispatch();
    this->invoke_to_captureControlReportIn(0, Fw::Enabled::ENABLED, false);
    this->component.doDispatch();
    ASSERT_EVENTS_SafeModeEntered_SIZE(0);

    this->invoke_to_captureControlReportIn(0, Fw::Enabled::DISABLED, false);
    this->component.doDispatch();
    ASSERT_EVENTS_SafeModeEntered_SIZE(1);
}

// The late report warning counts from each SAFE entry, so a second stuck entry still raises it.
void MissionAppTester::testSafeEntryWarningCountsFromEachEntry() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(1, Ops::SAFE, Fw::CmdResponse::OK));
    this->runTicks(6);
    ASSERT_EVENTS_SafeEntryUnconfirmed_SIZE(1);
    this->finishSafeEntry();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(2, Ops::BASE, Fw::CmdResponse::OK));

    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(3, Ops::SAFE, Fw::CmdResponse::OK));
    this->runTicks(6);
    ASSERT_EVENTS_SafeEntryUnconfirmed_SIZE(1);
}

// Until entry is confirmed, SAFE cannot be left, and a missing report is raised once.
void MissionAppTester::testSafeExitBlockedUntilEntryConfirmed() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(1, Ops::SAFE, Fw::CmdResponse::OK));
    this->invoke_to_cancelReportIn(0, MissionApp::SAFE_CANCEL_KEY);
    this->component.doDispatch();
    this->clearHistory();

    // The refusal names what is missing, not the latch or the stability timer.
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(2, Ops::BASE, Fw::CmdResponse::VALIDATION_ERROR));
    ASSERT_EVENTS_SafeEntryUnconfirmed_SIZE(1);
    ASSERT_EVENTS_SafeEntryUnconfirmed(0, 1);
    ASSERT_EVENTS_SafeExitBlocked_SIZE(0);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(3, Mode::SAFE));

    this->clearHistory();
    this->runTicks(6);
    ASSERT_EVENTS_SafeEntryUnconfirmed_SIZE(1);
    ASSERT_EVENTS_SafeEntryUnconfirmed(0, 1);
    this->runTicks(6);
    ASSERT_EVENTS_SafeEntryUnconfirmed_SIZE(1);

    this->invoke_to_captureControlReportIn(0, Fw::Enabled::DISABLED, false);
    this->component.doDispatch();
    ASSERT_EVENTS_SafeModeEntered_SIZE(1);
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(4, Ops::BASE, Fw::CmdResponse::OK));
}

// One rule in MissionApp: captures are inhibited in SAFE and enabled again on the way out.
void MissionAppTester::testCaptureControlFollowsSafeOnly() {
    this->clearHistory();
    this->sendCmd_SCHEDULE_COLLECTION(0, 1, 10);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(0, Mode::COLLECTING, 10);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(0, Mode::SCIENCE_READY, 128);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Mode::DOWNLINKING, 128);
    this->component.doDispatch();
    ASSERT_EVENTS_ModeChanged_SIZE(4);
    ASSERT_from_captureControlOut_SIZE(0);

    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(2, Ops::SAFE, Fw::CmdResponse::OK));
    ASSERT_from_captureControlOut_SIZE(1);
    ASSERT_from_captureControlOut(0, Fw::Enabled::DISABLED);
    this->finishSafeEntry();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(3, Ops::BASE, Fw::CmdResponse::OK));
    ASSERT_from_captureControlOut_SIZE(2);
    ASSERT_from_captureControlOut(1, Fw::Enabled::ENABLED);
}

// faultIn: an unknown cause is recorded as OTHER_FAULT, and the first latched cause is kept.
void MissionAppTester::testFaultCausesRecordedHonestly() {
    this->clearHistory();
    this->invoke_to_faultIn(0, Components::SafeCause::GROUND_COMMAND, false, 0);
    this->component.doDispatch();
    this->finishSafeEntry();
    ASSERT_EVENTS_SafeModeEntered(0, Components::SafeCause::OTHER_FAULT, 0);

    this->invoke_to_faultIn(0, Components::SafeCause::WATCHDOG, true, 0);
    this->component.doDispatch();
    this->invoke_to_faultIn(0, Components::SafeCause::LOW_POWER, true, 0);
    this->component.doDispatch();
    this->clearHistory();
    this->sendCmd_CLEAR_SAFE_LATCH(0, 1);
    this->component.doDispatch();
    ASSERT_EVENTS_SafeLatchCleared(0, Components::SafeCause::WATCHDOG);
}

void MissionAppTester::testBootInSafeParameter() {
    this->paramSet_BOOT_IN_SAFE(1, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->clearHistory();

    // What the component thread does first after a reset.
    this->component.preamble();
    this->finishSafeEntry();

    ASSERT_EVENTS_ModeChanged_SIZE(1);
    ASSERT_EVENTS_ModeChanged(0, Mode::SAFE);
    ASSERT_EVENTS_SafeModeEntered(0, Components::SafeCause::BOOT, 0);
    ASSERT_TLM_SafeEntryCause(0, Components::SafeCause::BOOT);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(1, Mode::SAFE));

    // No fault and no latch at boot, so the first ground exit passes.
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(2, Ops::BASE, Fw::CmdResponse::OK));
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(3, Mode::BASE));
}

void MissionAppTester::testBootInBaseByDefault() {
    // Parameters not loaded yet: start in BASE, say nothing.
    this->clearHistory();
    this->component.preamble();
    ASSERT_EVENTS_SIZE(0);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(1, Mode::BASE));

    // Parameter loaded at its default (0): still BASE.
    this->component.loadParameters();
    this->clearHistory();
    this->component.preamble();
    ASSERT_EVENTS_SIZE(0);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(2, Mode::BASE));
}

void MissionAppTester::testSimulateSafeFaultOnlyInLocalBuild() {
    this->clearHistory();
    this->sendCmd_SIMULATE_SAFE_FAULT(0, 1, Components::SafeCause::LOW_POWER, 1);
    this->component.doDispatch();
#ifndef NEUTRON2_LOCAL_YAMCS
    // Unit-test and flight builds: refused, nothing changes.
    ASSERT_CMD_RESPONSE(0, MissionAppComponentBase::OPCODE_SIMULATE_SAFE_FAULT, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_MissionCommandRejected(0, 3, 0);
    ASSERT_EVENTS_ModeChanged_SIZE(0);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(2, Mode::BASE));
#else
    ASSERT_CMD_RESPONSE(0, MissionAppComponentBase::OPCODE_SIMULATE_SAFE_FAULT, 1, Fw::CmdResponse::OK);
    this->finishSafeEntry();
    ASSERT_EVENTS_SafeModeEntered(0, Components::SafeCause::LOW_POWER, 1);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(2, Mode::SAFE));
#endif
}

void MissionAppTester::testSubsystemPlanMatchesMopMatrix() {
    this->clearHistory();

    // Walk all five operations modes in demo order.
    this->sendCmd_SCHEDULE_COLLECTION(0, 1, 10);                    // Base (queued): no new plan
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(0, Mode::COLLECTING, 10);          // Science Collection
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(0, Mode::SCIENCE_READY, 128);      // Science Processing
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Mode::DOWNLINKING, 128);        // Science Downlink
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Mode::BASE, 0);                 // Base
    this->component.doDispatch();
    this->sendCmd_SET_MODE(0, 2, Ops::SAFE);                        // Safe
    this->component.doDispatch();
    this->finishSafeEntry();

    const MopRow expected[] = {MOP_COLLECTION, MOP_PROCESSING, MOP_DOWNLINK, MOP_BASE, MOP_SAFE};
    ASSERT_EVENTS_SubsystemStatesRequested_SIZE(5);
    for (U32 row = 0; row < 5U; ++row) {
        SCOPED_TRACE(::testing::Message() << "plan row " << row);
        const MopRow& r = expected[row];
        ASSERT_EVENTS_SubsystemStatesRequested(row, r.mode, r.eps, r.obc, r.uhfTx, r.uhfRx, r.sband, r.gps, r.adcs,
                                               r.payload);
    }
}

void MissionAppTester::testOperationsModeTelemetryFollowsMode() {
    this->clearHistory();

    this->sendCmd_SCHEDULE_COLLECTION(0, 1, 10);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(0, Mode::COLLECTING, 10);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(0, Mode::SCIENCE_READY, 128);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Mode::DOWNLINKING, 128);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Mode::BASE, 0);
    this->component.doDispatch();
    this->sendCmd_SET_MODE(0, 2, Ops::SAFE);
    this->component.doDispatch();
    this->finishSafeEntry();

    // COLLECTION_PENDING is still Base by the MOP, so the first value is BASE.
    ASSERT_TLM_OperationsMode_SIZE(6);
    ASSERT_TLM_OperationsMode(0, Ops::BASE);
    ASSERT_TLM_OperationsMode(1, Ops::SCIENCE_COLLECTION);
    ASSERT_TLM_OperationsMode(2, Ops::SCIENCE_PROCESSING);
    ASSERT_TLM_OperationsMode(3, Ops::SCIENCE_DOWNLINK);
    ASSERT_TLM_OperationsMode(4, Ops::BASE);
    ASSERT_TLM_OperationsMode(5, Ops::SAFE);
}

void MissionAppTester::testFullDemoCycleWithModes() {
    // The FSR story with the CDR-style ground-declared downlink window, plus the
    // rejected moves an operator is most likely to try.
    this->clearHistory();

    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(1, Ops::SCIENCE_PROCESSING, Fw::CmdResponse::VALIDATION_ERROR));
    ASSERT_EVENTS_SetModeRejected(0, Ops::SCIENCE_PROCESSING, Mode::BASE, Reason::AUTOMATIC_ONLY);
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(2, Ops::SCIENCE_COLLECTION, Fw::CmdResponse::VALIDATION_ERROR));
    ASSERT_EVENTS_SetModeRejected(1, Ops::SCIENCE_COLLECTION, Mode::BASE, Reason::USE_SCHEDULE_COLLECTION);

    this->sendCmd_SCHEDULE_COLLECTION(0, 3, 10);
    this->component.doDispatch();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(4, Ops::SCIENCE_DOWNLINK, Fw::CmdResponse::VALIDATION_ERROR));
    ASSERT_EVENTS_SetModeRejected(2, Ops::SCIENCE_DOWNLINK, Mode::COLLECTION_PENDING, Reason::COLLECTION_ACTIVE);

    this->invoke_to_modeUpdateIn(0, Mode::COLLECTING, 10);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(0, Mode::SCIENCE_READY, 128);
    this->component.doDispatch();
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(5, Ops::SCIENCE_DOWNLINK, Fw::CmdResponse::OK));
    ASSERT_NO_FATAL_FAILURE(this->sendSetMode(6, Ops::BASE, Fw::CmdResponse::OK));

    ASSERT_EVENTS_ModeChanged_SIZE(5);
    ASSERT_EVENTS_ModeChanged(0, Mode::COLLECTION_PENDING);
    ASSERT_EVENTS_ModeChanged(1, Mode::COLLECTING);
    ASSERT_EVENTS_ModeChanged(2, Mode::SCIENCE_READY);
    ASSERT_EVENTS_ModeChanged(3, Mode::DOWNLINKING);
    ASSERT_EVENTS_ModeChanged(4, Mode::BASE);
    ASSERT_EVENTS_SetModeRejected_SIZE(3);
    ASSERT_EVENTS_SubsystemStatesRequested_SIZE(4);
    ASSERT_from_collectionRequestOut_SIZE(1);
    ASSERT_from_cancelRequestOut_SIZE(1);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(7, Mode::BASE));
}

void MissionAppTester::testReportModeAnswersSafe() {
    // The D2 promise holds in the new mode: one answer, no side effects.
    ASSERT_NO_FATAL_FAILURE(this->driveTo(Mode::SAFE));
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(1, Mode::SAFE));
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(2, Mode::SAFE));
}

void MissionAppTester::testExistingIdsUnchanged() {
    // Yamcs, Dennis's scripts and report_mode.py know these numbers. D5 only appends.
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::OPCODE_ENTER_BASE_MODE), 0x0U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::OPCODE_PING), 0x1U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::OPCODE_SCHEDULE_COLLECTION), 0x2U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::OPCODE_CANCEL_COLLECTION), 0x3U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::OPCODE_REPORT_MODE), 0x4U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::EVENTID_MODECHANGED), 0x0U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::EVENTID_MODEUPDATEREJECTED), 0x1U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::EVENTID_MISSIONCOMMANDREJECTED), 0x2U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::EVENTID_PONG), 0x3U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::EVENTID_COLLECTIONSCHEDULED), 0x4U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::EVENTID_MODEREPORT), 0x5U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::CHANNELID_CURRENTMODE), 0x0U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::CHANNELID_LASTSCHEDULEDDELAYSECONDS), 0x1U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::CHANNELID_PINGCOUNT), 0x2U);
    ASSERT_EQ(static_cast<U32>(MissionAppComponentBase::CHANNELID_MODEHEARTBEAT), 0x3U);
    // MissionMode numbers the ground already decodes, plus SAFE appended at 5.
    ASSERT_EQ(static_cast<int>(Mode::BASE), 0);
    ASSERT_EQ(static_cast<int>(Mode::COLLECTION_PENDING), 1);
    ASSERT_EQ(static_cast<int>(Mode::COLLECTING), 2);
    ASSERT_EQ(static_cast<int>(Mode::SCIENCE_READY), 3);
    ASSERT_EQ(static_cast<int>(Mode::DOWNLINKING), 4);
    ASSERT_EQ(static_cast<int>(Mode::SAFE), 5);
}

void MissionAppTester::testAutoReturnOffHoldsScienceReady() {
    // Default (0): the stored product leaves MissionApp in SCIENCE_READY, as before.
    ASSERT_NO_FATAL_FAILURE(this->driveTo(Mode::COLLECTING));
    this->invoke_to_modeUpdateIn(0, Mode::SCIENCE_READY, 128);
    this->component.doDispatch();
    ASSERT_EVENTS_ModeChanged_SIZE(1);
    ASSERT_EVENTS_ModeChanged(0, Mode::SCIENCE_READY);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(1, Mode::SCIENCE_READY));
}

void MissionAppTester::testAutoReturnOnGoesBackToBase() {
    this->paramSet_AUTO_RETURN_TO_BASE(1, Fw::ParamValid::VALID);
    this->component.loadParameters();
    ASSERT_NO_FATAL_FAILURE(this->driveTo(Mode::COLLECTING));

    // The product reaches the OBC: Science Processing, then straight back to BASE (deck step 3.4).
    this->invoke_to_modeUpdateIn(0, Mode::SCIENCE_READY, 128);
    this->component.doDispatch();
    ASSERT_EVENTS_ModeChanged_SIZE(2);
    ASSERT_EVENTS_ModeChanged(0, Mode::SCIENCE_READY);
    ASSERT_EVENTS_ModeChanged(1, Mode::BASE);
    ASSERT_EVENTS_SubsystemStatesRequested_SIZE(2);
    ASSERT_EVENTS_SubsystemStatesRequested(0, Ops::SCIENCE_PROCESSING, Request::ON, Request::ON, Request::TBC,
                                           Request::ON, Request::OFF, Request::ON, Request::SUN_TRACKING,
                                           Request::STANDBY);
    ASSERT_EVENTS_SubsystemStatesRequested(1, Ops::BASE, Request::ON, Request::ON, Request::TBC, Request::ON,
                                           Request::OFF, Request::ON, Request::FLAG_DEPENDENT, Request::OFF);
    ASSERT_EVENTS_ModeUpdateRejected_SIZE(0);
    // Nothing was queued, so nothing is cancelled.
    ASSERT_from_cancelRequestOut_SIZE(0);
    ASSERT_NO_FATAL_FAILURE(this->assertReportModeAnswers(1, Mode::BASE));

    // A later downlink (deck step 4) still works from BASE.
    this->clearHistory();
    this->invoke_to_modeUpdateIn(1, Mode::DOWNLINKING, 128);
    this->component.doDispatch();
    this->invoke_to_modeUpdateIn(1, Mode::BASE, 128);
    this->component.doDispatch();
    ASSERT_EVENTS_ModeUpdateRejected_SIZE(0);
    ASSERT_EVENTS_ModeChanged_SIZE(2);
    ASSERT_EVENTS_ModeChanged(0, Mode::DOWNLINKING);
    ASSERT_EVENTS_ModeChanged(1, Mode::BASE);
}

}  // namespace Components
