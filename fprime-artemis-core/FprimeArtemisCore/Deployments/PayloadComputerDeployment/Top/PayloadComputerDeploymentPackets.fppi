telemetry packets PayloadComputerDeploymentPackets {

  packet CDH id 1 group 1 {
    CdhCore.cmdDisp.CommandsDispatched
    CdhCore.cmdDisp.CommandsDropped
    #CdhCore.tlmSend.SendLevel # uncomment when using TlmPacketizer

    FileHandling.fileUplink.FilesReceived
    FileHandling.fileUplink.PacketsReceived
    FileHandling.fileDownlink.FilesSent
    FileHandling.fileDownlink.PacketsSent
    FileHandling.fileManager.CommandsExecuted

    PayloadComputerDeployment.cmdSeq.CS_LoadCommands
    PayloadComputerDeployment.cmdSeq.CS_CancelCommands
    PayloadComputerDeployment.cmdSeq.CS_CommandsExecuted
    PayloadComputerDeployment.cmdSeq.CS_SequencesCompleted
    ComCcsds.comQueue.comQueueDepth
    ComCcsds.comQueue.buffQueueDepth
    ComCcsds.commsBufferManager.TotalBuffs
    ComCcsds.commsBufferManager.CurrBuffs
    ComCcsds.commsBufferManager.HiBuffs

    PayloadComputerDeployment.rateGroup_1Hz.RgMaxTime
    PayloadComputerDeployment.rateGroup_0_5Hz.RgMaxTime
    PayloadComputerDeployment.rateGroup_0_25Hz.RgMaxTime
  }

  packet CDHErrors id 2 group 1 {
    CdhCore.$health.PingLateWarnings

    FileHandling.fileUplink.Warnings
    FileHandling.fileDownlink.Warnings
    FileHandling.fileManager.Errors

    PayloadComputerDeployment.cmdSeq.CS_Errors
    ComCcsds.commsBufferManager.NoBuffs
    ComCcsds.commsBufferManager.EmptyBuffs

    PayloadComputerDeployment.rateGroup_1Hz.RgCycleSlips
    PayloadComputerDeployment.rateGroup_0_5Hz.RgCycleSlips
    PayloadComputerDeployment.rateGroup_0_25Hz.RgCycleSlips
  }

  packet SystemRes1 id 5 group 2 {
    PayloadComputerDeployment.systemResources.MEMORY_TOTAL
    PayloadComputerDeployment.systemResources.MEMORY_USED
    PayloadComputerDeployment.systemResources.NON_VOLATILE_TOTAL
    PayloadComputerDeployment.systemResources.NON_VOLATILE_FREE
  }

  packet SystemRes3 id 6 group 2 {
    PayloadComputerDeployment.systemResources.CPU
    PayloadComputerDeployment.systemResources.CPU_00
    PayloadComputerDeployment.systemResources.CPU_01
    PayloadComputerDeployment.systemResources.CPU_02
    PayloadComputerDeployment.systemResources.CPU_03
    PayloadComputerDeployment.systemResources.CPU_04
    PayloadComputerDeployment.systemResources.CPU_05
    PayloadComputerDeployment.systemResources.CPU_06
    PayloadComputerDeployment.systemResources.CPU_07
    PayloadComputerDeployment.systemResources.CPU_08
    PayloadComputerDeployment.systemResources.CPU_09
    PayloadComputerDeployment.systemResources.CPU_10
    PayloadComputerDeployment.systemResources.CPU_11
    PayloadComputerDeployment.systemResources.CPU_12
    PayloadComputerDeployment.systemResources.CPU_13
    PayloadComputerDeployment.systemResources.CPU_14
    PayloadComputerDeployment.systemResources.CPU_15
  }

  packet DataProducts id 21 group 3 {
    DataProducts.dpCat.CatalogDps
    DataProducts.dpCat.DpsSent

    DataProducts.dpMgr.NumSuccessfulAllocations
    DataProducts.dpMgr.NumFailedAllocations
    DataProducts.dpMgr.NumDataProducts
    DataProducts.dpMgr.NumBytes

    DataProducts.dpWriter.NumBuffersReceived
    DataProducts.dpWriter.NumBytesWritten
    DataProducts.dpWriter.NumSuccessfulWrites
    DataProducts.dpWriter.NumFailedWrites
    DataProducts.dpWriter.NumErrors

    DataProducts.dpBufferManager.TotalBuffs
    DataProducts.dpBufferManager.CurrBuffs
    DataProducts.dpBufferManager.HiBuffs
    DataProducts.dpBufferManager.NoBuffs
    DataProducts.dpBufferManager.EmptyBuffs
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

} omit {
  CdhCore.cmdDisp.CommandErrors
  comDriver.BytesRecv
  comDriver.BytesSent
}
