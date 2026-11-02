module Components {
    @ EPS manager exposing stable mission-facing behavior.
    active component EpsManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        async input port run: Svc.Sched

        @ Driver status input
        async input port driverStatusIn: Components.EpsStatus

        @ Driver-provided per-rail readback validity and age
        async input port railSnapshotIn: Components.EpsRailSnapshot

        @ Driver request output
        output port driverRequestOut: Components.EpsCommand

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Request latest EPS status from driver
        async command REQUEST_EPS_STATUS

        @ Ping the EPS driver endpoint
        async command PING_EPS_DRIVER

        @ Request EPS driver protocol version/capabilities
        async command REQUEST_EPS_DRIVER_INFO

        @ Request one EPS rail state by rail/output ID
        async command REQUEST_EPS_RAIL(outputId: U32)

        @ Set one safe EPS rail state. confirm must be 1.
        async command SET_EPS_RAIL_STATE(outputId: U32, desiredState: U32, confirm: U32)

        @ Power-cycle one safe EPS rail. confirm must be 1.
        async command POWER_CYCLE_EPS_RAIL(outputId: U32, offMs: U32, confirm: U32)

        @ Request EPS charger status
        async command REQUEST_CHARGER_STATUS

        @ Set EPS charger state. confirm must be 1.
        async command SET_CHARGER_STATE(enable: U32, confirm: U32)

        @ Get PDU protocol and firmware information
        async command GET_PDU_INFO opcode 8

        @ Get the PDU summary and report each rail readback
        async command GET_PDU_STATUS opcode 9

        @ Get one rail including burn readback; ALL uses GET_ALL_PDU_RAILS
        async command GET_PDU_RAIL(rail: Components.PduRail) opcode 10

        @ Read all PDU rail enable states
        async command GET_ALL_PDU_RAILS opcode 11

        @ Set one safe rail; explicit CONFIRM is required
        async command SET_PDU_RAIL(rail: Components.PduSwitchableRail, desiredState: Components.PduDesiredState, confirmation: Components.PduConfirmation) opcode 12

        @ Current EPS state
        telemetry EpsHealthState: Components.HealthState

        @ Latest driver link state. 0=not configured, 1=queued to Teensy, 2=protocol OK, 3=error, 4=busy.
        telemetry DriverLinkState: U32

        @ Latest EPS driver protocol version
        telemetry DriverProtocolVersion: U32

        @ Latest switched rail bitmap from summary status
        telemetry RailStateBitmap: U32

        @ Latest driver reset cause
        telemetry DriverResetCause: U32

        @ Latest driver fault bitmap
        telemetry DriverFaultBitmap: U32

        @ Latest driver uptime in seconds
        telemetry DriverUptimeSeconds: U32

        @ Latest driver protocol status code
        telemetry LastDriverStatus: U32

        @ Latest driver opcode handled
        telemetry LastDriverOpcode: U32

        @ Manager heartbeat
        telemetry ManagerHeartbeat: U32

        @ Latest PDU firmware semantic version fields
        telemetry DriverFirmwareMajor: U8
        telemetry DriverFirmwareMinor: U8
        telemetry DriverFirmwarePatch: U8

        @ Latest PDU build flags: bit 0 hash known, bit 1 dirty build
        telemetry DriverBuildFlags: U8

        @ Latest 12-character lowercase Git hash, all zeroes when unknown
        telemetry DriverGitHash: string size 12

        @ Enable readback; UNKNOWN or STALE means the current state is not known.
        telemetry Rail3V3_1: Components.PduRailState id 20
        telemetry Rail3V3_2: Components.PduRailState id 21
        telemetry Rail5V_1: Components.PduRailState id 22
        telemetry Rail5V_2: Components.PduRailState id 23
        telemetry Rail5V_3: Components.PduRailState id 24
        telemetry Rail12V: Components.PduRailState id 25
        telemetry RailVBatt: Components.PduRailState id 26
        telemetry RailBurn1: Components.PduRailState id 27
        telemetry RailBurn2: Components.PduRailState id 28

        @ Masks use the nine-rail summary order; age is in driver scheduler ticks.
        telemetry RailKnownMask: U16 id 29
        telemetry RailFreshMask: U16 id 30
        telemetry RailMaximumAgeTicks: U32 id 31

        @ EPS status update event
        event EpsStatusUpdated(healthState: Components.HealthState, outputBitmap: U32, faultBitmap: U32) severity activity low format "EPS status health={} outputs={} faults={}"

        @ EPS command rejected by mission-facing guard
        event EpsCommandRejected(reason: U32, value: U32) severity warning low format "EPS command rejected reason={} value={}"

        @ One event per rail in an explicitly requested successful summary
        event PduSummaryRail(sequence: U32, commandOpcode: U32, rail: Components.PduRail, readback: Components.PduRailState) severity activity high format "PDU STATUS sequence={} opcode={} rail={} readback={}"

        @ Explicit PDU information request result
        event PduInfoResult(sequence: U32, commandOpcode: U32, protocol: U32, major: U32, minor: U32, patch: U32, flags: U32, hash: string size 12) severity activity high format "PDU INFO sequence={} opcode={} protocol={} firmware={}.{}.{} flags={} git={}"

        @ Operator command could not be accepted
        event PduCommandRejected(sequence: U32, commandOpcode: U32, reason: string size 24) severity warning low format "PDU command sequence={} opcode={} rejected={}"

        @ Operator command completed unsuccessfully; see driver event for error origin
        event PduCommandFailed(sequence: U32, commandOpcode: U32, link: U32, status: U32) severity warning low format "PDU command sequence={} opcode={} failed link={} status={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
