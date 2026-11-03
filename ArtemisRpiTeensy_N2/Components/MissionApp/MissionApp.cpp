#include "Components/MissionApp/MissionApp.hpp"

#include <limits>

namespace Components {

namespace {
constexpr U32 MAX_SCHEDULE_DELAY_SECONDS = 300U;
constexpr U32 REJECT_INVALID_SCHEDULE_DELAY_SECONDS = 1U;
constexpr U32 REJECT_INVALID_MODE_TRANSITION = 2U;
// Reason codes 3 and 4 (D5): why SIMULATE_SAFE_FAULT was refused, sent in the MissionCommandRejected event.
constexpr U32 REJECT_SIMULATION_NOT_LOCAL = 3U;
constexpr U32 REJECT_INVALID_SIMULATED_FAULT = 4U;
// Same as the FPP default of SAFE_EXIT_STABLE_SECONDS. Used until the parameter database loads.
constexpr U32 DEFAULT_SAFE_EXIT_STABLE_SECONDS = 10U;

using Request = Components::SubsystemRequest;

// What one operations mode asks of each subsystem: one row of the MOP v2.0 §8 "Hardware Status".
struct SubsystemPlan {
    Request::T eps;
    Request::T obc;
    Request::T uhfTx;
    Request::T uhfRx;
    Request::T sband;
    Request::T gps;
    Request::T adcs;
    Request::T payload;
};

// Indexed by OperationsMode. TBC entries are still open in the MOP itself.
constexpr SubsystemPlan MODE_PLANS[] = {
    //                    EPS           OBC           UHF-TX       UHF-RX       S-band       GPS          ADCS                     Payload
    /* BASE           */ {Request::ON, Request::ON, Request::TBC, Request::ON, Request::OFF, Request::ON, Request::FLAG_DEPENDENT, Request::OFF},
    /* SCI COLLECTION */ {Request::ON, Request::ON, Request::TBC, Request::ON, Request::OFF, Request::ON, Request::LVLH, Request::ACQUISITION},
    /* SCI PROCESSING */ {Request::ON, Request::ON, Request::TBC, Request::ON, Request::OFF, Request::ON, Request::SUN_TRACKING, Request::STANDBY},
    /* SCI DOWNLINK   */ {Request::ON, Request::ON, Request::ON, Request::ON, Request::ON, Request::ON, Request::TARGET_TRACKING, Request::OFF},
    /* SAFE           */ {Request::MINIMAL, Request::MINIMAL, Request::TBC, Request::ON, Request::OFF, Request::OFF, Request::OFF, Request::OFF},
};

// The MOP mode each demo step belongs to. COLLECTION_PENDING is Base with a collection queued (MOP §8.3).
Components::OperationsMode toOperationsMode(Components::MissionMode mode) {
    switch (mode) {
        case Components::MissionMode::COLLECTING:
            return Components::OperationsMode::SCIENCE_COLLECTION;
        case Components::MissionMode::SCIENCE_READY:
            return Components::OperationsMode::SCIENCE_PROCESSING;
        case Components::MissionMode::DOWNLINKING:
            return Components::OperationsMode::SCIENCE_DOWNLINK;
        case Components::MissionMode::SAFE:
            return Components::OperationsMode::SAFE;
        default:
            return Components::OperationsMode::BASE;
    }
}

// The demo step SET_MODE aims for. Returns false for a value outside the enum.
bool toMissionMode(Components::OperationsMode mode, Components::MissionMode& missionMode) {
    switch (mode) {
        case Components::OperationsMode::BASE:
            missionMode = Components::MissionMode::BASE;
            return true;
        case Components::OperationsMode::SCIENCE_COLLECTION:
            missionMode = Components::MissionMode::COLLECTING;
            return true;
        case Components::OperationsMode::SCIENCE_PROCESSING:
            missionMode = Components::MissionMode::SCIENCE_READY;
            return true;
        case Components::OperationsMode::SCIENCE_DOWNLINK:
            missionMode = Components::MissionMode::DOWNLINKING;
            return true;
        case Components::OperationsMode::SAFE:
            missionMode = Components::MissionMode::SAFE;
            return true;
        default:
            return false;
    }
}

// The two refusals that come from the SAFE exit guard: a latch is set, or the last fault is too recent.
// They get the SafeExitBlocked event, which says what is still missing, instead of a plain refusal.
bool isSafeExitBlock(Components::ModeRejectReason reason) {
    return (reason == Components::ModeRejectReason::SAFE_LATCHED) ||
           (reason == Components::ModeRejectReason::SAFE_NOT_STABLE);
}

// Only real fault causes may arrive on faultIn. Anything else is recorded as OTHER_FAULT.
bool isFaultCause(Components::SafeCause cause) {
    return (cause == Components::SafeCause::LOW_POWER) || (cause == Components::SafeCause::WATCHDOG) ||
           (cause == Components::SafeCause::ADCS_RATE) || (cause == Components::SafeCause::CONTACT_TIMEOUT) ||
           (cause == Components::SafeCause::OTHER_FAULT);
}
}  // namespace

MissionApp::MissionApp(const char* const compName)
    : MissionAppComponentBase(compName),
      m_currentMode(Components::MissionMode::BASE),
      m_lastScheduledDelaySeconds(0),
      m_pingCount(0),
      m_modeHeartbeat(0),
      m_lastTelemetryHeartbeat(0),
      m_safeCause(Components::SafeCause::NONE),
      m_latchCause(Components::SafeCause::NONE),
      m_safeLatched(false),
      m_faultSeen(false),
      m_ticksSinceFault(0) {}

MissionApp::~MissionApp() {}

// FSW-013 asks for a SAFE start after every reset. It is a parameter, off by default,
// because the CDR acceptance and demo scripts check for BASE right after startup.
// preamble() runs on this component's thread after the parameters load and before
// any command is dispatched, so nothing can slip in ahead of the SAFE entry.
void MissionApp::preamble() {
    if (this->bootInSafe()) {
        this->enterSafe(Components::SafeCause::BOOT);
    } else {
        // Tell ScienceApp the start-up mode as well, so the two start in step.
        this->publishMode();
    }
}

void MissionApp::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void MissionApp::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_modeHeartbeat += 1;
    // rateGroup1 runs at 1 Hz, so one tick is one second of the SAFE stability interval.
    if (this->m_faultSeen && (this->m_ticksSinceFault < std::numeric_limits<U32>::max())) {
        this->m_ticksSinceFault += 1U;
    }
    constexpr U32 TELEMETRY_PERIOD_TICKS = 30U;
    if ((this->m_modeHeartbeat - this->m_lastTelemetryHeartbeat) >= TELEMETRY_PERIOD_TICKS) {
        this->writeTelemetry();
        this->m_lastTelemetryHeartbeat = this->m_modeHeartbeat;
    }
}

// ScienceApp (port 0) and CommsApp (port 1) report the mode their work has reached: COLLECTING,
// SCIENCE_READY, DOWNLINKING or BASE. The transition table decides whether MissionApp follows.
void MissionApp::modeUpdateIn_handler(FwIndexType portNum, const Components::MissionMode& mode, U32 detail) {
    static_cast<void>(portNum);
    const bool moved = this->transitionToMode(mode, ModeSource::SUBSYSTEM, detail);
    // MVP deck step 3.4: with AUTO_RETURN_TO_BASE = 1, Science Processing ends as soon as the
    // product is on the OBC, and MissionApp goes back to BASE by itself.
    if (moved && (mode == Components::MissionMode::SCIENCE_READY) && this->autoReturnToBase()) {
        static_cast<void>(this->transitionToMode(Components::MissionMode::BASE, ModeSource::SUBSYSTEM, detail));
    }
}

// A fault source asks for SAFE. Nothing is connected to this port in either topology yet, so today SAFE
// comes only from SET_MODE SAFE, BOOT_IN_SAFE or, in local builds, SIMULATE_SAFE_FAULT.
void MissionApp::faultIn_handler(FwIndexType portNum, const Components::SafeCause& cause, bool latch, U32 detail) {
    static_cast<void>(portNum);
    static_cast<void>(detail);
    this->handleFault(cause, latch);
}

void MissionApp::ENTER_BASE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // In SAFE this is a Safe exit request, so it passes through the same exit guard as SET_MODE BASE.
    if (!this->transitionToMode(Components::MissionMode::BASE, ModeSource::GROUND, 0U)) {
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_MODE_TRANSITION, 0U);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->cancelCollection();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void MissionApp::PING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 token) {
    this->m_pingCount += 1;
    this->writeTelemetry();
    this->log_ACTIVITY_LO_Pong(token, this->m_pingCount);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void MissionApp::SCHEDULE_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 delaySeconds) {
    if ((delaySeconds == 0U) || (delaySeconds > MAX_SCHEDULE_DELAY_SECONDS)) {
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_SCHEDULE_DELAY_SECONDS, delaySeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (this->checkTransition(Components::MissionMode::COLLECTION_PENDING, ModeSource::GROUND) !=
        Components::ModeRejectReason::ALLOWED) {
        this->log_WARNING_LO_ModeUpdateRejected(
            Components::MissionMode::COLLECTION_PENDING,
            this->m_currentMode,
            delaySeconds);
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_MODE_TRANSITION, delaySeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_lastScheduledDelaySeconds = delaySeconds;
    this->applyMode(Components::MissionMode::COLLECTION_PENDING);
    this->log_ACTIVITY_HI_CollectionScheduled(delaySeconds);
    if (this->isConnected_collectionRequestOut_OutputPort(0)) {
        this->collectionRequestOut_out(0, delaySeconds);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void MissionApp::CANCEL_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // SAFE already dropped any queued collection when it was entered. This command must
    // not double as a Safe exit: leaving SAFE takes SET_MODE BASE or ENTER_BASE_MODE.
    if (this->m_currentMode == Components::MissionMode::SAFE) {
        this->log_WARNING_LO_ModeUpdateRejected(Components::MissionMode::BASE, this->m_currentMode, 0U);
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_MODE_TRANSITION, 0U);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (!this->transitionToMode(Components::MissionMode::BASE, ModeSource::GROUND, 0U)) {
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_MODE_TRANSITION, 0U);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->cancelCollection();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// REPORT_MODE answers one question from the ground: "which mission mode are you in?"
//
// It is read-only on purpose. It does not change m_currentMode, write telemetry,
// or call any output port, so an operator can send it in every mode, at any time,
// as often as needed, without disturbing a scheduled collection or a downlink.
//
// The command is async, so this runs on MissionApp's own thread, one queued
// message at a time. Every mode change also runs on this thread, so the value
// read here is never caught halfway through a transition.
void MissionApp::REPORT_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // The answer: one ModeReport event carrying the mode MissionApp owns right now.
    // The ground shows it as "Mission mode is BASE" (C&T Modes row 3).
    this->log_ACTIVITY_HI_ModeReport(this->m_currentMode);

    // Reply last. By the time F' reports this command complete (C&T Modes row 2),
    // the ModeReport event has already been handed to the event system.
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// SET_MODE is the one "switch operations mode" command (C&T Modes row 1). The operator
// names a MOP mode. MissionApp maps it to the demo step that represents it and asks the
// transition table. Only BASE, SAFE and SCIENCE_DOWNLINK can be entered this way: the
// other two modes start when the payload confirms them.
void MissionApp::SET_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::OperationsMode mode) {
    Components::MissionMode requested = this->m_currentMode;
    const Components::ModeRejectReason reason = toMissionMode(mode, requested)
        ? this->checkTransition(requested, ModeSource::GROUND)
        : Components::ModeRejectReason(Components::ModeRejectReason::NOT_IN_TABLE);
    if (reason != Components::ModeRejectReason::ALLOWED) {
        if (isSafeExitBlock(reason)) {
            this->reportSafeExitBlocked();
        } else {
            this->log_WARNING_LO_SetModeRejected(mode, this->m_currentMode, reason);
        }
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }

    if ((requested == Components::MissionMode::SAFE) && (this->m_currentMode != Components::MissionMode::SAFE)) {
        this->enterSafe(Components::SafeCause::GROUND_COMMAND);
    } else {
        static_cast<void>(this->transitionToMode(requested, ModeSource::GROUND, 0U));
        // SET_MODE BASE means plain Base with nothing queued, the same as ENTER_BASE_MODE.
        if (requested == Components::MissionMode::BASE) {
            this->cancelCollection();
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// Clears the fault latch. It does not leave SAFE by itself: SET_MODE BASE (or ENTER_BASE_MODE) must
// still pass the stability check in checkSafeExit().
void MissionApp::CLEAR_SAFE_LATCH_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Safe to repeat: clearing an empty latch reports NONE and changes nothing.
    const Components::SafeCause cleared = this->m_latchCause;
    this->m_safeLatched = false;
    this->m_latchCause = Components::SafeCause::NONE;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_SafeLatchCleared(cleared);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// A practice fault for laptop testing. It only acts in local Yamcs builds (NEUTRON2_LOCAL_YAMCS).
// Every other build, CDR and flight included, refuses it.
void MissionApp::SIMULATE_SAFE_FAULT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::SafeCause cause, U8 latch) {
#ifndef NEUTRON2_LOCAL_YAMCS
    static_cast<void>(cause);
    static_cast<void>(latch);
    this->log_WARNING_LO_MissionCommandRejected(REJECT_SIMULATION_NOT_LOCAL, 0U);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
#else
    if (!isFaultCause(cause) || (latch > 1U)) {
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_SIMULATED_FAULT, static_cast<U32>(cause.e));
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->handleFault(cause, latch == 1U);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
#endif
}

// The transition table. Every mode change, from every source, is decided here.
// The rules run top to bottom and the first one that applies wins.
Components::ModeRejectReason MissionApp::checkTransition(Components::MissionMode requested, ModeSource source) {
    const Components::MissionMode current = this->m_currentMode;

    // 1. SAFE entry: a fault or the ground may ask from any mode. A subsystem report never may.
    if (requested == Components::MissionMode::SAFE) {
        return (source == ModeSource::SUBSYSTEM) ? Components::ModeRejectReason::NOT_IN_TABLE
                                                 : Components::ModeRejectReason::ALLOWED;
    }

    // 2. Inside SAFE: only a ground command to BASE can leave, and only past the exit guard.
    if (current == Components::MissionMode::SAFE) {
        if ((source != ModeSource::GROUND) || (requested != Components::MissionMode::BASE)) {
            return Components::ModeRejectReason::SAFE_LOCKED;
        }
        return this->checkSafeExit();
    }

    // 3. Asking for the mode you are already in is accepted (unchanged from before SAFE existed).
    if (requested == current) {
        return Components::ModeRejectReason::ALLOWED;
    }

    // 4. Ground limits. The ground cannot declare that a capture started or that a product
    //    is stored: ScienceApp reports those once the payload confirms them.
    if (source == ModeSource::GROUND) {
        switch (requested) {
            case Components::MissionMode::COLLECTING:
                return Components::ModeRejectReason::USE_SCHEDULE_COLLECTION;
            case Components::MissionMode::SCIENCE_READY:
                return Components::ModeRejectReason::AUTOMATIC_ONLY;
            case Components::MissionMode::DOWNLINKING:
                return ((current == Components::MissionMode::COLLECTION_PENDING) ||
                        (current == Components::MissionMode::COLLECTING))
                           ? Components::ModeRejectReason::COLLECTION_ACTIVE
                           : Components::ModeRejectReason::ALLOWED;
            default:
                break;  // BASE and COLLECTION_PENDING follow the demo chain below
        }
    }

    // 5. The demo chain (unchanged from before SAFE existed). Every nominal mode may return to BASE.
    if (requested == Components::MissionMode::BASE) {
        return Components::ModeRejectReason::ALLOWED;
    }
    bool allowed = false;
    switch (current) {
        case Components::MissionMode::BASE:
            allowed = (requested == Components::MissionMode::COLLECTION_PENDING) ||
                      (requested == Components::MissionMode::COLLECTING) ||
                      (requested == Components::MissionMode::DOWNLINKING);
            break;
        case Components::MissionMode::COLLECTION_PENDING:
            allowed = (requested == Components::MissionMode::COLLECTING);
            break;
        case Components::MissionMode::COLLECTING:
            allowed = (requested == Components::MissionMode::SCIENCE_READY);
            break;
        case Components::MissionMode::SCIENCE_READY:
            allowed = (requested == Components::MissionMode::DOWNLINKING);
            break;
        default:
            break;  // DOWNLINKING only returns to BASE
    }
    return allowed ? Components::ModeRejectReason::ALLOWED : Components::ModeRejectReason::NOT_IN_TABLE;
}

// The SDD's other exit conditions (fresh health inputs within recovery limits) need
// EPS, ADCS and watchdog inputs that no manager sends to MissionApp yet.
Components::ModeRejectReason MissionApp::checkSafeExit() {
    if (this->m_safeLatched) {
        return Components::ModeRejectReason::SAFE_LATCHED;
    }
    if (this->m_faultSeen && (this->m_ticksSinceFault < this->safeExitStableSeconds())) {
        return Components::ModeRejectReason::SAFE_NOT_STABLE;
    }
    return Components::ModeRejectReason::ALLOWED;
}

// Ask the transition table. Refused: report why and return false. Allowed: move and return true.
bool MissionApp::transitionToMode(Components::MissionMode requested, ModeSource source, U32 detail) {
    const Components::ModeRejectReason reason = this->checkTransition(requested, source);
    if (reason != Components::ModeRejectReason::ALLOWED) {
        this->reportRejectedTransition(requested, reason, detail);
        return false;
    }
    if ((this->m_currentMode == Components::MissionMode::SAFE) && (requested != Components::MissionMode::SAFE)) {
        // Leaving SAFE: the exit guard passed, so forget this stay's cause and fault timer.
        this->m_safeCause = Components::SafeCause::NONE;
        this->m_faultSeen = false;
        this->m_ticksSinceFault = 0U;
    }
    this->applyMode(requested);
    return true;
}

// The one place a mode change takes effect: store it, tell ScienceApp, update telemetry, log ModeChanged
// and, when the MOP operations mode changes, announce what the new mode asks of the hardware.
void MissionApp::applyMode(Components::MissionMode mode) {
    const Components::OperationsMode before = toOperationsMode(this->m_currentMode);
    this->m_currentMode = mode;
    // First, so a SAFE entry reaches ScienceApp ahead of the cancel that enterSafe() sends after it.
    this->publishMode();
    this->writeTelemetry();
    this->log_ACTIVITY_HI_ModeChanged(this->m_currentMode);
    const Components::OperationsMode after = toOperationsMode(mode);
    if (after != before) {
        this->reportSubsystemPlan(after);
    }
}

// Go to SAFE: record why, move, drop any queued collection, and log SafeModeEntered (warning high).
void MissionApp::enterSafe(Components::SafeCause cause) {
    // Record the cause first so the telemetry written by applyMode already shows it.
    this->m_safeCause = cause;
    // Science is inhibited in SAFE (SDD v4.0 Table 5). applyMode tells ScienceApp, which refuses every new
    // capture from then on, and cancelCollection drops a queued one. A capture already running belongs to
    // PayloadManager and payload power to the EPS rail, so neither stops here: ScienceApp withholds that
    // capture's product when it ends.
    this->applyMode(Components::MissionMode::SAFE);
    this->cancelCollection();
    this->log_WARNING_HI_SafeModeEntered(cause, static_cast<U8>(this->m_safeLatched ? 1U : 0U));
}

// A fault from faultIn or SIMULATE_SAFE_FAULT. Restarts the stability timer, sets the latch when asked
// (the first latched cause is kept), then enters SAFE, or logs SafeFaultRepeated if already there.
void MissionApp::handleFault(Components::SafeCause cause, bool latch) {
    const Components::SafeCause recorded = isFaultCause(cause) ? cause : Components::SafeCause(Components::SafeCause::OTHER_FAULT);
    // Every fault restarts the stability timer, even while already in SAFE.
    this->m_faultSeen = true;
    this->m_ticksSinceFault = 0U;
    if (latch && !this->m_safeLatched) {
        this->m_safeLatched = true;
        this->m_latchCause = recorded;
    }
    if (this->m_currentMode == Components::MissionMode::SAFE) {
        this->writeTelemetry();
        this->log_WARNING_LO_SafeFaultRepeated(recorded, static_cast<U8>(this->m_safeLatched ? 1U : 0U));
        return;
    }
    // The table always lets a fault into SAFE. Asking anyway keeps every change in one place.
    if (this->checkTransition(Components::MissionMode::SAFE, ModeSource::FAULT) ==
        Components::ModeRejectReason::ALLOWED) {
        this->enterSafe(recorded);
    }
}

// Exit-guard refusals get SafeExitBlocked (what is still missing). Every other refusal gets ModeUpdateRejected.
void MissionApp::reportRejectedTransition(Components::MissionMode requested, Components::ModeRejectReason reason, U32 detail) {
    if (isSafeExitBlock(reason)) {
        this->reportSafeExitBlocked();
        return;
    }
    this->log_WARNING_LO_ModeUpdateRejected(requested, this->m_currentMode, detail);
}

// Tells the ground why SAFE cannot be left yet: the latch cause, calm seconds so far, seconds needed.
void MissionApp::reportSafeExitBlocked() {
    const U32 healthySeconds = this->m_faultSeen ? this->m_ticksSinceFault : 0U;
    this->log_WARNING_LO_SafeExitBlocked(this->m_latchCause, healthySeconds, this->safeExitStableSeconds());
}

// Sends the MOP v2.0 §8 hardware row for this operations mode (SubsystemStatesRequested). It only
// declares the plan: no hardware is switched here.
void MissionApp::reportSubsystemPlan(Components::OperationsMode mode) {
    const FwSizeType index = static_cast<FwSizeType>(mode.e);
    FW_ASSERT(index < FW_NUM_ARRAY_ELEMENTS(MODE_PLANS), static_cast<FwAssertArgType>(index));
    const SubsystemPlan& plan = MODE_PLANS[index];
    this->log_ACTIVITY_HI_SubsystemStatesRequested(mode, plan.eps, plan.obc, plan.uhfTx, plan.uhfRx, plan.sband,
                                                   plan.gps, plan.adcs, plan.payload);
}

// Parameter reads. VALID = loaded from the parameter file or set by PRM_SET, DEFAULT = the FPP default.
// Before the parameter database loads the read is INVALID, and the safe choice is used:
// 10 s, no boot in SAFE, no automatic return to BASE.
U32 MissionApp::safeExitStableSeconds() {
    Fw::ParamValid valid = Fw::ParamValid::INVALID;
    const U32 seconds = this->paramGet_SAFE_EXIT_STABLE_SECONDS(valid);
    return ((valid == Fw::ParamValid::VALID) || (valid == Fw::ParamValid::DEFAULT))
               ? seconds
               : DEFAULT_SAFE_EXIT_STABLE_SECONDS;
}

bool MissionApp::bootInSafe() {
    Fw::ParamValid valid = Fw::ParamValid::INVALID;
    const U8 value = this->paramGet_BOOT_IN_SAFE(valid);
    return ((valid == Fw::ParamValid::VALID) || (valid == Fw::ParamValid::DEFAULT)) && (value != 0U);
}

bool MissionApp::autoReturnToBase() {
    Fw::ParamValid valid = Fw::ParamValid::INVALID;
    const U8 value = this->paramGet_AUTO_RETURN_TO_BASE(valid);
    return ((valid == Fw::ParamValid::VALID) || (valid == Fw::ParamValid::DEFAULT)) && (value != 0U);
}

void MissionApp::publishMode() {
    if (this->isConnected_modeOut_OutputPort(0)) {
        this->modeOut_out(0, this->m_currentMode, 0U);
    }
}

void MissionApp::cancelCollection() {
    this->m_lastScheduledDelaySeconds = 0U;
    this->writeTelemetry();
    if (this->isConnected_cancelRequestOut_OutputPort(0)) {
        this->cancelRequestOut_out(0, 0U);
    }
}

void MissionApp::writeTelemetry() {
    this->tlmWrite_CurrentMode(this->m_currentMode);
    this->tlmWrite_LastScheduledDelaySeconds(this->m_lastScheduledDelaySeconds);
    this->tlmWrite_PingCount(this->m_pingCount);
    this->tlmWrite_ModeHeartbeat(this->m_modeHeartbeat);
    this->tlmWrite_OperationsMode(toOperationsMode(this->m_currentMode));
    this->tlmWrite_SafeEntryCause(this->m_safeCause);
    this->tlmWrite_SafeLatched(this->m_safeLatched);
}

}  // namespace Components
