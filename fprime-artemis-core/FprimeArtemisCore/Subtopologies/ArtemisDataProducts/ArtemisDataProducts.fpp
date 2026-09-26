@ DataProducts subtopology for the payload computer.
@
@ Derived from the framework's Svc/Subtopologies/DataProducts. The one
@ structural change: dpWriter's write notification is exported as
@ dpWrittenOut instead of being wired straight to dpCat, so the topology can
@ notify the payload driver that produced the file as well as the catalog
@ (see DpWrittenRouter). Buffer sizes are also raised to hold camera frames.
module ArtemisDataProducts {

    # ----------------------------------------------------------------------
    # Active Components
    # ----------------------------------------------------------------------
    
    instance dpCat: Svc.DpCatalog base id ArtemisDataProductsConfig.BASE_ID + 0x00000 \
        queue size ArtemisDataProductsConfig.QueueSizes.dpCat \
        stack size ArtemisDataProductsConfig.StackSizes.dpCat \
        priority ArtemisDataProductsConfig.Priorities.dpCat \
        cpu ArtemisDataProductsConfig.CpuAffinities.dpCat \
    {
        phase Fpp.ToCpp.Phases.configComponents """
            Fw::FileNameString dpDir(ArtemisDataProductsConfig::Paths::dpDir);
            Fw::FileNameString dpState(ArtemisDataProductsConfig::Paths::dpState);
            Os::FileSystem::createDirectory(dpDir.toChar());
            Fw::ExternalArray<Fw::FileNameString> dpDirs(&dpDir, 1);
            ArtemisDataProducts::dpCat.configure(dpDirs, dpState, 0, ArtemisDataProducts::Allocation::memAllocator);
        """
    }

    instance dpMgr: Svc.DpManager base id ArtemisDataProductsConfig.BASE_ID + 0x01000 \
        queue size ArtemisDataProductsConfig.QueueSizes.dpMgr \
        stack size ArtemisDataProductsConfig.StackSizes.dpMgr \
        priority ArtemisDataProductsConfig.Priorities.dpMgr \
        cpu ArtemisDataProductsConfig.CpuAffinities.dpMgr

    instance dpWriter: Svc.DpWriter base id ArtemisDataProductsConfig.BASE_ID + 0x02000 \
        queue size ArtemisDataProductsConfig.QueueSizes.dpWriter \
        stack size ArtemisDataProductsConfig.StackSizes.dpWriter \
        priority ArtemisDataProductsConfig.Priorities.dpWriter \
        cpu ArtemisDataProductsConfig.CpuAffinities.dpWriter \
    {
        phase Fpp.ToCpp.Phases.configComponents """
            ArtemisDataProducts::dpWriter.configure(dpDir);
        """
    }

    instance dpBufferAccumulator: Svc.BufferAccumulator base id ArtemisDataProductsConfig.BASE_ID + 0x04000 \
        queue size ArtemisDataProductsConfig.QueueSizes.dpBufferAccumulator \
        stack size ArtemisDataProductsConfig.StackSizes.dpBufferAccumulator \
        priority ArtemisDataProductsConfig.Priorities.dpBufferAccumulator \
        cpu ArtemisDataProductsConfig.CpuAffinities.dpBufferAccumulator \
    {
        phase Fpp.ToCpp.Phases.configComponents """
            ArtemisDataProducts::dpBufferAccumulator.allocateQueue(
                ArtemisDataProductsConfig::BufferAccumulator::allocatorId,
                ArtemisDataProducts::Allocation::memAllocator,
                ArtemisDataProductsConfig::BufferAccumulator::maxNumBuffers,
                Svc::BufferAccumulator_OpState::DRAIN
            );
        """
        phase Fpp.ToCpp.Phases.tearDownComponents """
            ArtemisDataProducts::dpBufferAccumulator.deallocateQueue(ArtemisDataProducts::Allocation::memAllocator);
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
        #Active Components
        instance dpCat
        instance dpMgr
        instance dpWriter
        instance dpBufferAccumulator

        #Passive Components
        instance dpBufferManager

        connections ArtemisDataProducts {
            # DpMgr, BufferAccumulator, and DpWriter connections. Have explicit port indexes for demo
            dpMgr.bufferGetOut[0] -> dpBufferManager.bufferGetCallee
            dpMgr.productSendOut[0] -> dpBufferAccumulator.bufferSendInFill
            dpBufferAccumulator.bufferSendOutDrain -> dpWriter.bufferSendIn
            dpWriter.deallocBufferSendOut -> dpBufferAccumulator.bufferSendInReturn
            dpBufferAccumulator.bufferSendOutReturn -> dpBufferManager.bufferSendIn

            # dpWriter.dpWrittenOut is exported below instead of wired to
            # dpCat.addToCat here; the topology routes it through DpWrittenRouter.
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
        port dpCatFileOut  = dpCat.fileOut

        @ Input port for receiving file downlink completion notifications
        port dpCatFileDone = dpCat.fileDone

        @ Input port for scheduling dpBufferManager telemetry output
        port dpBufferManagerSchedIn = dpBufferManager.schedIn

        @ Input port for scheduling dpWriter telemetry output
        port dpWriterSchedIn        = dpWriter.schedIn

        @ Output port for processing data products
        port dpWriterProcOut        = dpWriter.procBufferSendOut

        @ Input port for scheduling dpMgr telemetry output
        port dpMgrSchedIn           = dpMgr.schedIn

    } # end topology
} # end ArtemisDataProducts Subtopology
