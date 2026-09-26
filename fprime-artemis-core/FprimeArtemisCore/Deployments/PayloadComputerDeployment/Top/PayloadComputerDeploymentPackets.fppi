telemetry packets PayloadComputerDeploymentPackets {

  packet CDH id 1 group 1 {
    CdhCore.cmdDisp.CommandsDispatched
    CdhCore.cmdDisp.CommandsDropped
    CdhCore.events.EventsDropped
    #CdhCore.tlmSend.SendLevel # uncomment when using TlmPacketizer

    FileHandling.fileUplink.FilesReceived
    FileHandling.fileUplink.PacketsReceived
    FileHandling.fileUplink.FilesReceivedFailed
    ArtemisDataProducts.dpBufferAccumulator.BA_NumQueuedBuffers
    FileHandling.fileDownlink.FilesSent
    FileHandling.fileDownlink.PacketsSent
    FileHandling.fileManager.CommandsExecuted

    cmdSeq.CS_LoadCommands
    cmdSeq.CS_CancelCommands
    cmdSeq.CS_CommandsExecuted
    cmdSeq.CS_SequencesCompleted
    ComCcsds.comQueue.comQueueDepth
    ComCcsds.comQueue.buffQueueDepth
    CdhCore.tlmSend.GroupConfigs
    CdhCore.tlmSend.SectionEnabled
    ComCcsds.commsBufferManager.TotalBuffs
    ComCcsds.commsBufferManager.CurrBuffs
    ComCcsds.commsBufferManager.HiBuffs

    rateGroup_1Hz.RgMaxTime
    rateGroup_0_5Hz.RgMaxTime
    rateGroup_0_25Hz.RgMaxTime
  }

  packet CDHErrors id 2 group 1 {
    CdhCore.$health.PingLateWarnings

    FileHandling.fileUplink.Warnings
    FileHandling.fileDownlink.Warnings
    FileHandling.fileManager.Errors

    cmdSeq.CS_Errors
    ComCcsds.commsBufferManager.NoBuffs
    ComCcsds.commsBufferManager.EmptyBuffs

    rateGroup_1Hz.RgCycleSlips
    rateGroup_0_5Hz.RgCycleSlips
    rateGroup_0_25Hz.RgCycleSlips
  }

  packet SystemRes1 id 5 group 2 {
    systemResources.MEMORY_TOTAL
    systemResources.MEMORY_USED
    systemResources.NON_VOLATILE_TOTAL
    systemResources.NON_VOLATILE_FREE
  }

  packet SystemRes3 id 6 group 2 {
    systemResources.CPU
    systemResources.CPU_00
    systemResources.CPU_01
    systemResources.CPU_02
    systemResources.CPU_03
    systemResources.CPU_04
    systemResources.CPU_05
    systemResources.CPU_06
    systemResources.CPU_07
    systemResources.CPU_08
    systemResources.CPU_09
    systemResources.CPU_10
    systemResources.CPU_11
    systemResources.CPU_12
    systemResources.CPU_13
    systemResources.CPU_14
    systemResources.CPU_15
  }

  packet DataProducts id 21 group 3 {
    ArtemisDataProducts.dpCat.CatalogDps
    ArtemisDataProducts.dpCat.DpsSent

    ArtemisDataProducts.dpMgr.NumSuccessfulAllocations
    ArtemisDataProducts.dpMgr.NumFailedAllocations
    ArtemisDataProducts.dpMgr.NumDataProducts
    ArtemisDataProducts.dpMgr.NumBytes

    ArtemisDataProducts.dpWriter.NumBuffersReceived
    ArtemisDataProducts.dpWriter.NumBytesWritten
    ArtemisDataProducts.dpWriter.NumSuccessfulWrites
    ArtemisDataProducts.dpWriter.NumFailedWrites
    ArtemisDataProducts.dpWriter.NumErrors

    ArtemisDataProducts.dpBufferManager.TotalBuffs
    ArtemisDataProducts.dpBufferManager.CurrBuffs
    ArtemisDataProducts.dpBufferManager.HiBuffs
    ArtemisDataProducts.dpBufferManager.NoBuffs
    ArtemisDataProducts.dpBufferManager.EmptyBuffs
  }

  packet Version1 id 22 group 2 {
    CdhCore.version.FrameworkVersion
    CdhCore.version.ProjectVersion
  }

  packet Version_Library1 id 23 group 2 {
    CdhCore.version.LibraryVersion01
    CdhCore.version.LibraryVersion02
  }

  packet Version_Library2 id 24 group 2 {
    CdhCore.version.LibraryVersion03
    CdhCore.version.LibraryVersion04
  }

  packet Version_Library3 id 25 group 2 {
    CdhCore.version.LibraryVersion05
    CdhCore.version.LibraryVersion06
  }

  packet Version_Library4 id 26 group 2 {
    CdhCore.version.LibraryVersion07
    CdhCore.version.LibraryVersion08
  }

  packet Version_Library5 id 27 group 2 {
    CdhCore.version.LibraryVersion09
    CdhCore.version.LibraryVersion10
  }

  packet Version_Custom1 id 28 group 2 {
    CdhCore.version.CustomVersion01
  }

  packet Version_Custom2 id 29 group 2 {
    CdhCore.version.CustomVersion02
  }

  packet Version_Custom3 id 30 group 2 {
    CdhCore.version.CustomVersion03
  }

  packet Version_Custom4 id 31 group 2 {
    CdhCore.version.CustomVersion04
  }

  packet Version_Custom5 id 32 group 2 {
    CdhCore.version.CustomVersion05
  }

  packet Version_Custom6 id 33 group 2 {
    CdhCore.version.CustomVersion06
  }

  packet Version_Custom7 id 34 group 2 {
    CdhCore.version.CustomVersion07
  }

  packet Version_Custom8 id 35 group 2 {
    CdhCore.version.CustomVersion08
  }

  packet Version_Custom9 id 36 group 2 {
    CdhCore.version.CustomVersion09
  }

  packet Version_Custom10 id 37 group 2 {
    CdhCore.version.CustomVersion10
  }

  packet Payload id 4 group 1 {
    payloadManager.PayloadState
    payloadManager.LastPayloadValue
    payloadManager.LastCollectionId
    payloadManager.LastCaptureDurationSeconds
    payloadManager.SampleCount
    payloadManager.SamplePeriodMs
    payloadManager.ManagerHeartbeat

    payloadDriverLepton.PayloadState
    payloadDriverLepton.LastDurationSeconds
    payloadDriverLepton.LastProductId
    payloadDriverLepton.LastDataBytes
    payloadDriverLepton.LastFileBytes
    payloadDriverLepton.LastCaptureStatus
    payloadDriverLepton.PendingWrites
  }

  packet FcLink id 3 group 1 {
    fcLinkManager.HeartbeatsSent
    fcLinkBufferManager.TotalBuffs
    fcLinkBufferManager.CurrBuffs
    fcLinkBufferManager.HiBuffs
    fcLinkBufferManager.NoBuffs
    fcLinkBufferManager.EmptyBuffs
  }

} omit {
  CdhCore.cmdDisp.CommandErrors
  # String channel: TlmPacketizer cannot packetize variable-length strings
  cmdSeq.CS_CurrentSequence
  fcLinkDriver.BytesRecv
  fcLinkDriver.BytesSent
}
