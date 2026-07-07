module Components {
    @ Storage manager managing science staging and downlink readiness.
    active component StorageManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Science product input from ScienceApp
        sync input port requestIn: Components.ScienceProductDescriptor

        @ Downlink request input from CommsApp
        sync input port downlinkRequestIn: Components.ScienceDownlinkRequest

        @ Science availability output to CommsApp
        output port downlinkReadyOut: Components.ScienceDownlinkReady

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Report storage status
        async command REPORT_STORAGE_STATUS

        @ Report the latest stored science dataset
        async command REPORT_LATEST_DATASET

        @ Emit one event per remembered stored dataset
        async command REPORT_STORAGE_HISTORY

        @ Remove old simulator capture CSVs from the RPi temp capture directory. Use confirm=1.
        async command REMOVE_OLD_DATASETS(confirm: U32)

        @ Number of stored products
        telemetry StoredProducts: U32

        @ Last stored product size
        telemetry LastProductSize: U32

        @ Number of products remembered in the local storage history
        telemetry StorageHistoryDepth: U32

        @ Number of dataset files removed by the last cleanup command
        telemetry RemovedDatasetFiles: U32

        @ Number of dataset files that failed removal in the last cleanup command
        telemetry RemoveDatasetFailures: U32

        @ Stored product event
        event ScienceStored(productCount: U32, productBytes: U32) severity activity high format "Stored science product count={} size={}"

        @ Latest stored product report
        event LatestDataset(productCount: U32, productBytes: U32) severity activity high format "Latest science dataset count={} size={}"

        @ Stored product history row
        event StorageHistoryEntry(slot: U32, productCount: U32, productBytes: U32) severity activity low format "Storage history slot={} count={} size={}"

        @ Empty storage history report
        event StorageHistoryEmpty() severity activity low format "Storage history empty"

        @ Downlink preparation event
        event DownlinkPrepared(downlinkBytes: U32) severity activity low format "Prepared science downlink size={}"

        @ Old dataset cleanup rejected
        event RemoveOldDatasetsRejected(confirm: U32) severity warning low format "Remove old datasets rejected confirm={}"

        @ Old dataset cleanup completed
        event OldDatasetsRemoved(removedFiles: U32, failedFiles: U32) severity activity high format "Removed old dataset files={} failures={}"

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
