module Components {
    @ Storage manager managing science staging and downlink readiness.
    active component StorageManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        async input port run: Svc.Sched

        @ Science product input from ScienceApp
        async input port requestIn: Components.ScienceProductDescriptor

        @ Explicit automatic-delivery request for a product already stored here.
        async input port deliveryRequestIn: Components.ScienceProductDeliveryRequest

        @ Downlink request input from CommsApp
        async input port downlinkRequestIn: Components.ScienceDownlinkRequest

        @ Science availability output to CommsApp
        output port downlinkReadyOut: Components.ScienceDownlinkReady

        @ Answer to requestIn: the product was committed, or rejected
        output port productStoredOut: Components.ScienceProductStored

        @ Exact validated descriptor for an automatic delivery request.
        output port deliveryDescriptorOut: Components.ScienceProductDeliveryDescriptor

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Report storage status
        async command REPORT_STORAGE_STATUS

        @ Report the latest stored science dataset
        async command REPORT_LATEST_DATASET

        @ Emit one event per remembered stored dataset
        async command REPORT_STORAGE_HISTORY

        @ Remove transient simulator capture files from /tmp only. Durable catalog products are retained. Use confirm=1.
        async command REMOVE_OLD_DATASETS(confirm: U32)

        @ Select an immutable stored product for a later CommsApp science downlink.
        async command SELECT_PRODUCT(productId: U32)

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

        @ Product selected as the current downlink source; zero means none selected.
        telemetry SelectedProductId: U32

        @ Whether the durable product store and catalog are available.
        telemetry StorageReady: bool

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

        @ Product descriptor rejected before storage or downlink publication.
        event ScienceProductRejected(productId: U32, reason: U32) severity warning high format "Rejected science product id={} reason={}"

        @ Durable product available in the recovered storage catalog.
        event StorageCatalogEntry(slot: U32, productId: U32, productBytes: U32, sourceCrc: U32, sourcePath: string size 192) severity activity low format "Storage catalog slot={} product={} bytes={} CRC={} path='{}'"

        @ Valid durable product recovered after initialization.
        event ScienceProductRecovered(productId: U32, productBytes: U32, sourceCrc: U32, sourcePath: string size 192) severity activity high format "Recovered science product={} bytes={} CRC={} path='{}'"

        @ New opaque bytes and descriptor durably committed to storage.
        event ScienceProductCommitted(productId: U32, productBytes: U32, sourceCrc: U32, sourcePath: string size 192) severity activity high format "Committed science product={} bytes={} CRC={} path='{}'"

        @ Product selected as the current downlink source.
        event ProductSelected(productId: U32, productBytes: U32, sourceCrc: U32, sourcePath: string size 192) severity activity high format "Selected science product={} bytes={} CRC={} path='{}'"

        @ Detailed path for a rejected catalog product or incoming descriptor.
        event ScienceProductRejectedDetail(productId: U32, reason: U32, sourcePath: string size 192) severity warning low format "Rejected science product={} reason={} path='{}'"

        @ Durable storage root is unavailable or its catalog exceeds configured capacity.
        event StorageUnavailable(reason: U32) severity warning high format "Science storage unavailable reason={}"

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
