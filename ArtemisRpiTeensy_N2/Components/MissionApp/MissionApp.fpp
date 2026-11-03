module Components {
    @ Mission-level mode and collection scheduler manager.
    active component MissionApp {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        async input port run: Svc.Sched

        @ Collection scheduling output to ScienceApp
        output port collectionRequestOut: Components.CollectionRequest

        @ Collection cancellation output to ScienceApp
        output port cancelRequestOut: Svc.Ping

        @ The current mission mode, sent on every change and at start-up. ScienceApp refuses every
        @ capture while it reads SAFE (SDD v4.0 Table 5: science inhibited in Safe).
        output port modeOut: Components.MissionModeUpdate

        @ Mission mode update input from story managers
        async input port modeUpdateIn: [2] Components.MissionModeUpdate

        @ Fault indications that put the spacecraft in SAFE (FSW-007).
        @ Not connected in either topology yet: no manager publishes a fault for N2 today.
        async input port faultIn: Components.SafeModeRequest

        @ Enter base mode
        async command ENTER_BASE_MODE

        @ Simple local ping command for manual GDS loop checks
        async command PING(token: U32)

        @ Schedule data collection delay in seconds
        async command SCHEDULE_COLLECTION(delaySeconds: U32)

        @ Cancel pending collection and return to base mode
        async command CANCEL_COLLECTION

        @ Report the current mission mode as one ModeReport event.
        @ Read-only: changes no mode, telemetry or output port, so it is safe to send in every mode.
        async command REPORT_MODE

        @ Switch operations mode (C&T Modes row 1). The transition table decides.
        @ Ground can pick BASE, SAFE or SCIENCE_DOWNLINK. SCIENCE_COLLECTION starts through
        @ SCHEDULE_COLLECTION, and SCIENCE_PROCESSING starts on its own when a product is stored.
        async command SET_MODE(mode: Components.OperationsMode)

        @ Clear a latched SAFE fault. Leaving SAFE still needs the stability interval to pass.
        async command CLEAR_SAFE_LATCH

        @ Act as if a fault indication arrived on faultIn. latch: 1 = latch, 0 = no latch.
        @ This command is rejected unless the deployment is built with NEUTRON2_LOCAL_YAMCS.
        async command SIMULATE_SAFE_FAULT(cause: Components.SafeCause, latch: U8)

        @ Seconds with no new fault before a ground command may leave SAFE (SDD v4.0 §7.2).
        @ PROVISIONAL: the real value belongs to the fault table (OD-03).
        param SAFE_EXIT_STABLE_SECONDS: U32 default 10

        @ 1 = start in SAFE after every reset (FSW-013). 0 = start in BASE, which the
        @ current demo and acceptance scripts expect.
        param BOOT_IN_SAFE: U8 default 0

        @ 1 = go back to BASE as soon as a stored product arrives (MVP deck step 3.4).
        @ 0 = stay in SCIENCE_READY until a downlink or the ground, which the current scripts expect.
        param AUTO_RETURN_TO_BASE: U8 default 0

        @ Current mission mode
        telemetry CurrentMode: Components.MissionMode update on change

        @ Last requested schedule delay
        telemetry LastScheduledDelaySeconds: U32 update on change

        @ Number of pings handled
        telemetry PingCount: U32 update on change

        @ Mission manager heartbeat
        telemetry ModeHeartbeat: U32

        @ Current operations mode by its MOP v2.0 §8 name (C&T Modes row 3, mission-operations view)
        telemetry OperationsMode: Components.OperationsMode update on change

        @ Why the spacecraft is in SAFE. NONE outside SAFE.
        telemetry SafeEntryCause: Components.SafeCause update on change

        @ True while a latched fault holds SAFE
        telemetry SafeLatched: bool update on change

        @ Mission mode transition
        event ModeChanged(mode: Components.MissionMode) severity activity high format "Mission mode changed to {}"

        @ Invalid manager-requested mission mode transition rejected; keep throttled because port paths can storm.
        event ModeUpdateRejected(requested: Components.MissionMode, current: Components.MissionMode, detail: U32) severity warning low format "Rejected mode update requested={} current={} detail={}" throttle 5

        @ Mission command rejected by validation guard
        event MissionCommandRejected(reason: U32, value: U32) severity warning low format "Mission command rejected reason={} value={}"

        @ Ping response event
        event Pong(token: U32, count: U32) severity activity low format "MissionApp pong token={} count={}"

        @ Collection scheduling event
        event CollectionScheduled(delaySeconds: U32) severity activity high format "Collection scheduled in {}s"

        @ Answer to REPORT_MODE: the mission mode at the moment the command ran.
        event ModeReport(mode: Components.MissionMode) severity activity high format "Mission mode is {}"

        @ SET_MODE refused by the transition table. Safe-exit refusals use SafeExitBlocked instead.
        event SetModeRejected(
            requested: Components.OperationsMode,
            current: Components.MissionMode,
            reason: Components.ModeRejectReason
        ) severity warning low format "SET_MODE {} refused in {}: {}"

        @ MissionApp entered SAFE. latched = 1 means a fault latch also holds it there.
        event SafeModeEntered(cause: Components.SafeCause, latched: U8) severity warning high format "SAFE mode entered cause={} latched={}"

        @ Another fault arrived while already in SAFE. The stability timer restarts.
        event SafeFaultRepeated(cause: Components.SafeCause, latched: U8) severity warning low format "Fault {} while in SAFE latched={}, stability timer restarted" throttle 5

        @ A ground command asked to leave SAFE and the exit guard refused (SDD v4.0 §7.2).
        event SafeExitBlocked(
            latchedCause: Components.SafeCause,
            healthySeconds: U32,
            requiredSeconds: U32
        ) severity warning low format "SAFE exit blocked: latch={} healthy {}s of {}s"

        @ The ground cleared the SAFE fault latch. NONE means nothing was latched.
        event SafeLatchCleared(cause: Components.SafeCause) severity activity high format "SAFE latch cleared (was {})"

        @ What the new operations mode asks of each subsystem (MOP v2.0 §8 hardware status).
        @ MissionApp declares the plan. It does not switch hardware itself.
        event SubsystemStatesRequested(
            mode: Components.OperationsMode,
            eps: Components.SubsystemRequest,
            obc: Components.SubsystemRequest,
            uhfTx: Components.SubsystemRequest,
            uhfRx: Components.SubsystemRequest,
            sband: Components.SubsystemRequest,
            gps: Components.SubsystemRequest,
            adcs: Components.SubsystemRequest,
            payload: Components.SubsystemRequest
        ) severity activity high format "{} asks EPS={} OBC={} UHF-TX={} UHF-RX={} S-band={} GPS={} ADCS={} Payload={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel

        @ Parameter get port
        param get port prmGetOut

        @ Parameter set port
        param set port prmSetOut
    }
}
