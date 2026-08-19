module ArtemisRpiTeensyDeployment {

  # ----------------------------------------------------------------------
  # Symbolic constants for port numbers
  # ----------------------------------------------------------------------

  enum Ports_RateGroups {
    rateGroup1
    rateGroup2
    rateGroup3
  }

  topology ArtemisRpiTeensyDeployment {

  # ----------------------------------------------------------------------
  # Subtopology imports
  # ----------------------------------------------------------------------
    import CdhCore.Subtopology
    import ComCcsds.Subtopology
    import ArtemisDataProducts.Subtopology
    import FileHandling.Subtopology
    
  # ----------------------------------------------------------------------
  # Instances used in the topology
  # ----------------------------------------------------------------------
    instance chronoTime
    instance rateGroup1
    instance rateGroup2
    instance rateGroup3
    instance rateGroupDriver
    instance systemResources
    instance timer
    instance comDriver
    instance cmdSeq
    instance teensyTransportManager
    instance missionApp
    instance scienceApp
    instance sohApp
    instance commsApp
    instance epsManager
    instance payloadManager
    instance adcsManager
    instance gpsManager
    instance storageManager
    instance thermalManager
    instance payloadDownlinkApp
    instance payloadStreamApp
    instance epsDriverArtemis
    instance payloadDriverSelector
    instance payloadDriverLepton
    instance payloadDriverBoson
    instance payloadDriverNeutronSim
    instance adcsDriverD2S2
    instance gpsDriverArtemis
    instance commsDriverTeensyRfm23
    instance thermalDriverArtemis
    instance uartChannelMux
    instance dpWrittenRouter

  # ----------------------------------------------------------------------
  # Pattern graph specifiers
  # ----------------------------------------------------------------------

    command connections instance CdhCore.cmdDisp
    event connections instance CdhCore.events
    telemetry connections instance CdhCore.tlmSend
    text event connections instance CdhCore.textLogger
    health connections instance CdhCore.$health
    param connections instance FileHandling.prmDb
    time connections instance chronoTime

  # ----------------------------------------------------------------------
  # Telemetry packets (only used when TlmPacketizer is used)
  # ----------------------------------------------------------------------

    # include "ArtemisRpiTeensyDeploymentPackets.fppi"

  # ----------------------------------------------------------------------
  # Direct graph specifiers
  # ----------------------------------------------------------------------

    connections ComCcsds_CdhCore {
      # Core events and telemetry to communication queue
      CdhCore.events.PktSend -> ComCcsds.comQueue.comPacketQueueIn[ComCcsds.Ports_ComPacketQueue.EVENTS]
      CdhCore.tlmSend.PktSend -> ComCcsds.comQueue.comPacketQueueIn[ComCcsds.Ports_ComPacketQueue.TELEMETRY]

      # Router to Command Dispatcher
      ComCcsds.fprimeRouter.commandOut -> CdhCore.cmdDisp.seqCmdBuff
      CdhCore.cmdDisp.seqCmdStatus -> ComCcsds.fprimeRouter.cmdResponseIn
      
    }

    connections ComCcsds_FileHandling {
      # File Downlink to Communication Queue
      FileHandling.fileDownlink.bufferSendOut -> ComCcsds.comQueue.bufferQueueIn[ComCcsds.Ports_ComBufferQueue.FILE]
      ComCcsds.comQueue.bufferReturnOut[ComCcsds.Ports_ComBufferQueue.FILE] -> FileHandling.fileDownlink.bufferReturn

      # Router to File Uplink
      ComCcsds.fprimeRouter.fileOut -> FileHandling.fileUplink.bufferSendIn
      FileHandling.fileUplink.bufferSendOut -> ComCcsds.fprimeRouter.fileBufferReturnIn
    }

    connections Communications {
      # ComDriver buffer allocations
      comDriver.allocate      -> ComCcsds.commsBufferManager.bufferGetCallee
      comDriver.deallocate    -> ComCcsds.commsBufferManager.bufferSendIn
      
      # ComDriver <-> UART channel mux <-> ComStub (Uplink)
      comDriver.$recv                      -> uartChannelMux.drvReceiveIn
      uartChannelMux.drvReceiveReturnOut   -> comDriver.recvReturnIn
      uartChannelMux.ccsdsRecvOut          -> ComCcsds.comStub.drvReceiveIn
      ComCcsds.comStub.drvReceiveReturnOut -> uartChannelMux.ccsdsRecvReturnIn
      
      # ComStub <-> UART channel mux <-> ComDriver (Downlink)
      ComCcsds.comStub.drvSendOut -> uartChannelMux.ccsdsSendIn
      uartChannelMux.drvSendOut   -> comDriver.$send
      comDriver.ready         -> ComCcsds.comStub.drvConnected

      # Channel 1 carries generic payload blob packets outside CCSDS.
      uartChannelMux.payloadRecvOut -> payloadDownlinkApp.packetIn
      payloadDownlinkApp.packetOut -> uartChannelMux.payloadSendIn
      payloadDownlinkApp.cacheRequestOut -> uartChannelMux.payloadCacheSendIn
      uartChannelMux.payloadCacheRecvOut -> payloadDownlinkApp.cacheResponseIn
      payloadStreamApp.previewPacketOut -> uartChannelMux.previewSendIn
      uartChannelMux.previewRecvOut -> payloadStreamApp.previewResponseIn
      payloadStreamApp.responseAdvanceOut -> payloadStreamApp.responseAdvanceIn
    }

    connections FileHandling_DataProducts {
      # Data Products to File Downlink
      ArtemisDataProducts.dpCat.fileOut -> FileHandling.fileDownlink.SendFile
      FileHandling.fileDownlink.FileComplete -> ArtemisDataProducts.dpCat.fileDone
    }

    connections DataProducts_DpWritten {
      ArtemisDataProducts.dpWriter.dpWrittenOut -> dpWrittenRouter.dpWrittenIn
      dpWrittenRouter.catalogOut -> ArtemisDataProducts.dpCat.addToCat
      dpWrittenRouter.leptonNotifyOut -> payloadDriverLepton.dpWrittenIn
      dpWrittenRouter.bosonNotifyOut -> payloadDriverBoson.dpWrittenIn
    }

    connections RateGroups {
      # timer to drive rate group
      timer.CycleOut -> rateGroupDriver.CycleIn

      # Rate group 1
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup1] -> rateGroup1.CycleIn
      rateGroup1.RateGroupMemberOut[0] -> CdhCore.tlmSend.Run
      rateGroup1.RateGroupMemberOut[1] -> FileHandling.fileDownlink.Run
      # Bound the one pending Teensy-local radio RPC at one-second resolution.
      rateGroup1.RateGroupMemberOut[2] -> commsDriverTeensyRfm23.run
      # RF MVP: keep automatic downlink volume low; command-triggered paths remain active.
      rateGroup1.RateGroupMemberOut[3] -> ComCcsds.comQueue.run
      rateGroup1.RateGroupMemberOut[4] -> ComCcsds.aggregator.timeout
      rateGroup1.RateGroupMemberOut[5] -> teensyTransportManager.run
      rateGroup1.RateGroupMemberOut[6] -> missionApp.run
      rateGroup1.RateGroupMemberOut[7] -> payloadDownlinkApp.run
      # RF MVP: tick the scheduled science path; keep higher-volume demo status loops off.
      rateGroup1.RateGroupMemberOut[8] -> scienceApp.run
      rateGroup1.RateGroupMemberOut[9] -> payloadStreamApp.run

      # Rate group 2
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup2] -> rateGroup2.CycleIn
      rateGroup2.RateGroupMemberOut[0] -> cmdSeq.schedIn
      rateGroup2.RateGroupMemberOut[1] -> epsDriverArtemis.run
      # Slow radio policy tick: boot reconciliation, capped backoff, and status polling.
      rateGroup2.RateGroupMemberOut[2] -> commsApp.run
      # rateGroup2.RateGroupMemberOut[3] -> adcsManager.run
      # rateGroup2.RateGroupMemberOut[4] -> gpsManager.run
      # rateGroup2.RateGroupMemberOut[5] -> storageManager.run
      # rateGroup2.RateGroupMemberOut[6] -> thermalManager.run

      # Rate group 3
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup3] -> rateGroup3.CycleIn
      rateGroup3.RateGroupMemberOut[0] -> CdhCore.$health.Run
      rateGroup3.RateGroupMemberOut[1] -> ComCcsds.commsBufferManager.schedIn
      rateGroup3.RateGroupMemberOut[2] -> ArtemisDataProducts.dpBufferManager.schedIn
      rateGroup3.RateGroupMemberOut[3] -> ArtemisDataProducts.dpWriter.schedIn
      rateGroup3.RateGroupMemberOut[4] -> ArtemisDataProducts.dpMgr.schedIn
      rateGroup3.RateGroupMemberOut[5] -> comDriver.run
    }

    connections CdhCore_cmdSeq {
      # Command Sequencer
      cmdSeq.comCmdOut -> CdhCore.cmdDisp.seqCmdBuff
      CdhCore.cmdDisp.seqCmdStatus -> cmdSeq.cmdResponseIn
    }

    connections MissionFlow {
      missionApp.collectionRequestOut -> scienceApp.requestIn
      missionApp.cancelRequestOut -> scienceApp.cancelRequestIn
      scienceApp.payloadRequestOut -> payloadManager.requestIn
      payloadManager.statusOut -> scienceApp.payloadStatusIn
      scienceApp.scienceProductOut -> storageManager.requestIn
      storageManager.downlinkReadyOut -> commsApp.scienceReadyIn
      commsApp.payloadDownlinkRequestOut -> payloadDownlinkApp.downlinkRequestIn
      payloadDownlinkApp.statusOut -> commsApp.payloadDownlinkStatusIn
      scienceApp.missionModeOut -> missionApp.modeUpdateIn[0]
      commsApp.missionModeOut -> missionApp.modeUpdateIn[1]
    }

    connections ManagerDriverBindings {
      epsManager.driverRequestOut -> epsDriverArtemis.requestIn
      epsDriverArtemis.statusOut -> epsManager.driverStatusIn
      epsDriverArtemis.teensyRequestOut -> uartChannelMux.localSendIn
      uartChannelMux.localRecvOut -> epsDriverArtemis.teensyResponseIn

      payloadManager.driverRequestOut -> payloadDriverSelector.requestIn
      payloadDriverSelector.driverRequestOut[0] -> payloadDriverLepton.requestIn
      payloadDriverSelector.driverRequestOut[1] -> payloadDriverBoson.requestIn
      payloadDriverSelector.deactivateDriverOut[0] -> payloadDriverLepton.deactivateIn
      payloadDriverSelector.deactivateDriverOut[1] -> payloadDriverBoson.deactivateIn
      payloadDriverLepton.statusOut -> payloadDriverSelector.driverStatusIn[0]
      payloadDriverBoson.statusOut -> payloadDriverSelector.driverStatusIn[1]
      payloadDriverSelector.statusOut -> payloadManager.driverStatusIn

      payloadDriverLepton.productGetOut -> ArtemisDataProducts.dpMgr.productGetIn
      payloadDriverLepton.productSendOut -> ArtemisDataProducts.dpMgr.productSendIn
      payloadStreamApp.previewRequestOut -> payloadDriverLepton.previewRequestIn
      payloadDriverLepton.previewOut -> payloadStreamApp.previewIn
      payloadDriverBoson.productGetOut -> ArtemisDataProducts.dpMgr.productGetIn
      payloadDriverBoson.productSendOut -> ArtemisDataProducts.dpMgr.productSendIn

      adcsManager.driverRequestOut -> adcsDriverD2S2.requestIn
      adcsDriverD2S2.statusOut -> adcsManager.driverStatusIn

      gpsManager.driverRequestOut -> gpsDriverArtemis.requestIn
      gpsDriverArtemis.statusOut -> gpsManager.driverStatusIn

      thermalManager.driverRequestOut -> thermalDriverArtemis.requestIn
      thermalDriverArtemis.statusOut -> thermalManager.driverStatusIn

      commsApp.driverRequestOut -> commsDriverTeensyRfm23.requestIn
      commsDriverTeensyRfm23.teensyRequestOut -> uartChannelMux.rfLocalSendIn
      uartChannelMux.rfLocalRecvOut -> commsDriverTeensyRfm23.teensyResponseIn
      commsDriverTeensyRfm23.statusOut -> commsApp.driverStatusIn
      commsDriverTeensyRfm23.rfRxCountOut -> teensyTransportManager.driverStatusIn
    }

    connections SoHInputs {
      epsManager.sohStatusOut -> sohApp.statusIn[0]
      payloadManager.sohStatusOut -> sohApp.statusIn[1]
      adcsManager.sohStatusOut -> sohApp.statusIn[2]
      gpsManager.sohStatusOut -> sohApp.statusIn[3]
      storageManager.sohStatusOut -> sohApp.statusIn[4]
      thermalManager.sohStatusOut -> sohApp.statusIn[5]
      commsApp.sohStatusOut -> sohApp.statusIn[6]
      # CommsApp is the authoritative radio/Teensy health owner. The legacy
      # transport manager infers RF contact from recent uplink packet counts,
      # so radio silence is diagnostic data, not a bus failure.
    }

    connections ArtemisRpiTeensyDeployment {

    }

  }

}
