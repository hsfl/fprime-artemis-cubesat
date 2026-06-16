module Components {
    @ PDU service skeleton for mission-facing power control and telemetry.
    active component PduService {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Adapter status input
        sync input port adapterStatusIn: Svc.Ping

        @ Adapter request output
        output port adapterRequestOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Svc.Ping

        @ Request latest PDU status from adapter
        async command REQUEST_PDU_STATUS

        @ Record requested payload 28 V rail state. This does not actuate hardware.
        async command SET_PAYLOAD_28V_REQUEST(requestState: U32)

        @ Record requested SatNOGS radio power state. This does not actuate hardware.
        async command SET_SATNOGS_POWER_REQUEST(requestState: U32)

        @ Current PDU health state
        telemetry PduHealthState: U32

        @ Payload 28 V rail request state
        telemetry Payload28VRequestState: U32

        @ SatNOGS radio power request state
        telemetry SatnogsPowerRequestState: U32

        @ Service heartbeat
        telemetry ServiceHeartbeat: U32

        @ PDU status update event
        event PduStatusUpdated(statusKey: U32) severity activity low format "PDU status updated state={}"

        @ Payload 28 V rail request event
        event Payload28VRequestRecorded(requestState: U32) severity activity high format "Payload 28V request state={}"

        @ SatNOGS radio power request event
        event SatnogsPowerRequestRecorded(requestState: U32) severity activity high format "SatNOGS power request state={}"

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
