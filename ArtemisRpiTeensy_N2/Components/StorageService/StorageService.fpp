module Components {
    @ Storage service managing science staging and downlink readiness.
    active component StorageService {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Science product input from ScienceManager
        sync input port requestIn: Svc.Ping

        @ Downlink request input from CommsManager
        sync input port downlinkRequestIn: Svc.Ping

        @ Science availability output to CommsManager
        output port downlinkReadyOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Svc.Ping

        @ Report storage status
        async command REPORT_STORAGE_STATUS

        @ Number of stored products
        telemetry StoredProducts: U32

        @ Last stored product size
        telemetry LastProductSize: U32

        @ Stored product event
        event ScienceStored(productCount: U32, productBytes: U32) severity activity high format "Stored science product count={} size={}"

        @ Downlink preparation event
        event DownlinkPrepared(downlinkBytes: U32) severity activity low format "Prepared science downlink size={}"

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
