module ArtemisDataProducts {

    # ----------------------------------------------------------------------
    # Active Components
    # ----------------------------------------------------------------------

    instance dpCat: Svc.DpCatalog base id ArtemisDataProductsConfig.BASE_ID + 0x00000 \
        queue size ArtemisDataProductsConfig.QueueSizes.dpCat \
        stack size ArtemisDataProductsConfig.StackSizes.dpCat \
        priority ArtemisDataProductsConfig.Priorities.dpCat \
    {
        phase Fpp.ToCpp.Phases.configComponents """
            Fw::FileNameString dpDir(ArtemisDataProductsConfig::Paths::dpDir);
            Fw::FileNameString dpState(ArtemisDataProductsConfig::Paths::dpState);
            Os::FileSystem::createDirectory(dpDir.toChar());
            ArtemisDataProducts::dpCat.configure(&dpDir,1,dpState,0, ArtemisDataProducts::Allocation::memAllocator);
        """
    }

    instance dpMgr: Svc.DpManager base id ArtemisDataProductsConfig.BASE_ID + 0x01000 \
        queue size ArtemisDataProductsConfig.QueueSizes.dpMgr \
        stack size ArtemisDataProductsConfig.StackSizes.dpMgr \
        priority ArtemisDataProductsConfig.Priorities.dpMgr

    instance dpWriter: Svc.DpWriter base id ArtemisDataProductsConfig.BASE_ID + 0x02000 \
        queue size ArtemisDataProductsConfig.QueueSizes.dpWriter \
        stack size ArtemisDataProductsConfig.StackSizes.dpWriter \
        priority ArtemisDataProductsConfig.Priorities.dpWriter \
    {
        phase Fpp.ToCpp.Phases.configComponents """
            ArtemisDataProducts::dpWriter.configure(dpDir);
        """
    }

    # ----------------------------------------------------------------------
    # Passive Components
    # ----------------------------------------------------------------------

    instance dpBufferManager: Svc.BufferManager base id ArtemisDataProductsConfig.BASE_ID + 0x03000 \
    {
        phase Fpp.ToCpp.Phases.configObjects """
        Svc::BufferManager::BufferBins bins;
        """
        phase Fpp.ToCpp.Phases.configComponents """
        memset(&ConfigObjects::ArtemisDataProducts_dpBufferManager::bins, 0, sizeof(ConfigObjects::ArtemisDataProducts_dpBufferManager::bins));
        ConfigObjects::ArtemisDataProducts_dpBufferManager::bins.bins[0].bufferSize = ArtemisDataProductsConfig::BuffMgr::dpBufferStoreSize;
        ConfigObjects::ArtemisDataProducts_dpBufferManager::bins.bins[0].numBuffers = ArtemisDataProductsConfig::BuffMgr::dpBufferStoreCount;
        ArtemisDataProducts::dpBufferManager.setup(
            ArtemisDataProductsConfig::BuffMgr::dpBufferManagerId,
            0,
            ArtemisDataProducts::Allocation::memAllocator,
            ConfigObjects::ArtemisDataProducts_dpBufferManager::bins
        );
        """
        phase Fpp.ToCpp.Phases.tearDownComponents """
        ArtemisDataProducts::dpCat.shutdown();
        ArtemisDataProducts::dpBufferManager.cleanup();
        """
    }

    topology Subtopology {
        # Active Components
        instance dpCat
        instance dpMgr
        instance dpWriter

        # Passive Components
        instance dpBufferManager

        connections ArtemisDataProducts {
            dpMgr.bufferGetOut[0] -> dpBufferManager.bufferGetCallee
            dpMgr.productSendOut[0] -> dpWriter.bufferSendIn
            dpWriter.deallocBufferSendOut -> dpBufferManager.bufferSendIn
        }

        # ----------------------------------------------------------------------
        # Topology ports
        # ----------------------------------------------------------------------

        @ Input port array for responding to data product get requests from client components
        port productGetIn       = dpMgr.productGetIn

        @ Input port array for receiving data product buffer requests from client components
        port productRequestIn   = dpMgr.productRequestIn

        @ Input port array for receiving filled data product buffers from client components
        port productSendIn      = dpMgr.productSendIn

        @ Output port array for sending requested data product buffers to client components
        port productResponseOut = dpMgr.productResponseOut

        @ Output port for data product write notifications
        port dpWrittenOut = dpWriter.dpWrittenOut

        @ Input port for adding data product files to the catalog
        port dpCatAddIn = dpCat.addToCat

        @ Output port for sending file downlink requests to a file downlink component
        port dpCatFileOut = dpCat.fileOut

        @ Input port for receiving file downlink completion notifications
        port dpCatFileDone = dpCat.fileDone

        @ Input port for scheduling dpBufferManager telemetry output
        port dpBufferManagerSchedIn = dpBufferManager.schedIn

        @ Input port for scheduling dpWriter telemetry output
        port dpWriterSchedIn = dpWriter.schedIn

        @ Input port for scheduling dpMgr telemetry output
        port dpMgrSchedIn = dpMgr.schedIn

    }
}
