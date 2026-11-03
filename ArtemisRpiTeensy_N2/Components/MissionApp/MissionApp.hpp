#ifndef Components_MissionApp_HPP
#define Components_MissionApp_HPP

#include "Components/MissionApp/MissionAppComponentAc.hpp"

namespace Components {

class MissionApp final : public MissionAppComponentBase {
  public:
    MissionApp(const char* const compName);
    ~MissionApp();
    //! Runs once on MissionApp's own thread before the first message. Applies BOOT_IN_SAFE (FSW-013).
    void preamble() override;

  private:
    //! Who asked for a mode change. The transition table has one column per source.
    enum class ModeSource : U8 {
        GROUND,     //!< a ground command
        SUBSYSTEM,  //!< a ScienceApp or CommsApp report on modeUpdateIn
        FAULT       //!< a fault indication, or BOOT_IN_SAFE at startup
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void modeUpdateIn_handler(FwIndexType portNum, const Components::MissionMode& mode, U32 detail) override;
    //! A fault source asks for SAFE. Nothing is connected to it in either topology yet.
    void faultIn_handler(FwIndexType portNum, const Components::SafeCause& cause, bool latch, U32 detail) override;
    void ENTER_BASE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void PING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 token) override;
    void SCHEDULE_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 delaySeconds) override;
    void CANCEL_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    //! Read-only: emits one ModeReport event with the current mode, then replies OK.
    void REPORT_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    //! Switch operations mode by MOP name (C&T Modes row 1). A refusal says why.
    void SET_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::OperationsMode mode) override;
    //! Clears the fault latch. Leaving SAFE still takes SET_MODE BASE past the stability check.
    void CLEAR_SAFE_LATCH_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    //! Local Yamcs builds only: acts as if a fault arrived on faultIn. Refused in every other build.
    void SIMULATE_SAFE_FAULT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::SafeCause cause, U8 latch) override;

    //! The transition table: ALLOWED, or the reason the move is refused. Changes nothing.
    Components::ModeRejectReason checkTransition(Components::MissionMode requested, ModeSource source);
    //! The SAFE exit guard (SDD v4.0 §7.2): no latch, and no fault inside the stability interval.
    Components::ModeRejectReason checkSafeExit();
    //! Asks checkTransition, then refuses with the reason or moves. True when MissionApp moved.
    bool transitionToMode(Components::MissionMode requested, ModeSource source, U32 detail);
    //! Every accepted mode change ends here: ScienceApp is told, then telemetry, ModeChanged, the subsystem plan.
    void applyMode(Components::MissionMode mode);
    //! Sends the current mode on modeOut. ScienceApp refuses every capture while it reads SAFE.
    void publishMode();
    //! Records the cause, moves to SAFE (ScienceApp stops new captures), drops queued science, logs SafeModeEntered.
    void enterSafe(Components::SafeCause cause);
    //! A fault: restarts the stability timer, sets the latch when asked, enters SAFE.
    void handleFault(Components::SafeCause cause, bool latch);
    //! Picks the refusal event: SafeExitBlocked for the exit guard, ModeUpdateRejected otherwise.
    void reportRejectedTransition(Components::MissionMode requested, Components::ModeRejectReason reason, U32 detail);
    //! Why SAFE cannot be left yet: latch cause, calm seconds so far, seconds required.
    void reportSafeExitBlocked();
    //! Logs the MOP v2.0 §8 hardware row for an operations mode. Declares only, switches nothing.
    void reportSubsystemPlan(Components::OperationsMode mode);
    //! Parameter reads, with safe fallbacks until the parameter database loads: 10 s, boot in BASE, no auto-return.
    U32 safeExitStableSeconds();
    bool bootInSafe();
    bool autoReturnToBase();
    void cancelCollection();
    void writeTelemetry();

    Components::MissionMode m_currentMode;
    U32 m_lastScheduledDelaySeconds;
    U32 m_pingCount;
    U32 m_modeHeartbeat;
    U32 m_lastTelemetryHeartbeat;
    Components::SafeCause m_safeCause;   //!< why we are in SAFE, NONE outside it
    Components::SafeCause m_latchCause;  //!< which fault set the latch, NONE when clear
    //! Set by a latching fault, cleared only by CLEAR_SAFE_LATCH. While set, SAFE cannot be left.
    bool m_safeLatched;
    bool m_faultSeen;                    //!< a fault arrived since SAFE was entered
    U32 m_ticksSinceFault;               //!< 1 Hz run ticks since the last fault (rateGroup1)
};

}  // namespace Components

#endif
