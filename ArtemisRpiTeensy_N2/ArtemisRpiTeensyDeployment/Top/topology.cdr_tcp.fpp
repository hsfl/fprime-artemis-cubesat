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
    import DataProducts.Subtopology
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
    instance epsManager
    instance payloadManager
    instance adcsManager
    instance gpsManager
    instance storageManager
    instance thermalManager
    instance epsDriverArtemis
    instance payloadDriverNeutronSim
    instance adcsDriverD2S2
    instance gpsDriverArtemis
    instance thermalDriverArtemis
    instance uartChannelMux
    instance tcpServer

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
      # Both TCP and UART receive buffers use ComCcsds' common pool.
      tcpServer.allocate      -> ComCcsds.commsBufferManager.bufferGetCallee
      tcpServer.deallocate    -> ComCcsds.commsBufferManager.bufferSendIn
      comDriver.allocate      -> ComCcsds.commsBufferManager.bufferGetCallee
      comDriver.deallocate    -> ComCcsds.commsBufferManager.bufferSendIn

      # CCSDS command, telemetry, and FileDownlink use TCP directly.
      tcpServer.$recv                      -> ComCcsds.comStub.drvReceiveIn
      ComCcsds.comStub.drvReceiveReturnOut -> tcpServer.recvReturnIn
      ComCcsds.comStub.drvSendOut          -> tcpServer.$send
      tcpServer.ready                      -> ComCcsds.comStub.drvConnected

      # The physical UART carries only Teensy-local RPC through mux channel 2.
      comDriver.$recv                      -> uartChannelMux.drvReceiveIn
      uartChannelMux.drvReceiveReturnOut   -> comDriver.recvReturnIn
      uartChannelMux.drvSendOut   -> comDriver.$send
    }

    connections FileHandling_DataProducts {
      # Data Products to File Downlink
      DataProducts.dpCat.fileOut -> FileHandling.fileDownlink.SendFile
      FileHandling.fileDownlink.FileComplete -> DataProducts.dpCat.fileDone
    }

    connections RateGroups {
      # timer to drive rate group
      timer.CycleOut -> rateGroupDriver.CycleIn

      # Rate group 1
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup1] -> rateGroup1.CycleIn
      rateGroup1.RateGroupMemberOut[0] -> CdhCore.tlmSend.Run
      rateGroup1.RateGroupMemberOut[1] -> FileHandling.fileDownlink.Run
      # RF MVP: keep automatic downlink volume low; command-triggered paths remain active.
      # rateGroup1.RateGroupMemberOut[2] -> systemResources.run
      rateGroup1.RateGroupMemberOut[3] -> ComCcsds.comQueue.run
      rateGroup1.RateGroupMemberOut[4] -> ComCcsds.aggregator.timeout
      rateGroup1.RateGroupMemberOut[5] -> teensyTransportManager.run
      rateGroup1.RateGroupMemberOut[6] -> missionApp.run
      # RF MVP: tick the scheduled science path; keep higher-volume demo status loops off.
      rateGroup1.RateGroupMemberOut[8] -> scienceApp.run
      # rateGroup1.RateGroupMemberOut[9] -> sohApp.run

      # Rate group 2
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup2] -> rateGroup2.CycleIn
      rateGroup2.RateGroupMemberOut[0] -> cmdSeq.schedIn
      rateGroup2.RateGroupMemberOut[1] -> epsDriverArtemis.run
      rateGroup2.RateGroupMemberOut[2] -> payloadManager.run
      rateGroup2.RateGroupMemberOut[3] -> payloadDriverNeutronSim.run
      # rateGroup2.RateGroupMemberOut[3] -> adcsManager.run
      # rateGroup2.RateGroupMemberOut[4] -> gpsManager.run
      rateGroup2.RateGroupMemberOut[5] -> storageManager.run
      # Query the PDU through UART mux channel 2; independent of the TCP client.
      rateGroup2.RateGroupMemberOut[6] -> epsManager.run
      # rateGroup2.RateGroupMemberOut[7] -> thermalManager.run

      # Rate group 3
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup3] -> rateGroup3.CycleIn
      rateGroup3.RateGroupMemberOut[0] -> CdhCore.$health.Run
      rateGroup3.RateGroupMemberOut[1] -> ComCcsds.commsBufferManager.schedIn
      rateGroup3.RateGroupMemberOut[2] -> DataProducts.dpBufferManager.schedIn
      rateGroup3.RateGroupMemberOut[3] -> DataProducts.dpWriter.schedIn
      rateGroup3.RateGroupMemberOut[4] -> DataProducts.dpMgr.schedIn
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
      payloadManager.operationNoticeOut -> scienceApp.operationNoticeIn
      scienceApp.scienceProductOut -> storageManager.requestIn
      scienceApp.deliveryRequestOut -> storageManager.deliveryRequestIn
      scienceApp.missionModeOut -> missionApp.modeUpdateIn[0]
    }

    connections ManagerDriverBindings {
      epsManager.driverRequestOut -> epsDriverArtemis.requestIn
      epsDriverArtemis.statusOut -> epsManager.driverStatusIn
      epsDriverArtemis.railSnapshotOut -> epsManager.railSnapshotIn
      epsDriverArtemis.teensyRequestOut -> uartChannelMux.localSendIn
      uartChannelMux.localRecvOut -> epsDriverArtemis.teensyResponseIn

      payloadManager.driverRequestOut -> payloadDriverNeutronSim.requestIn
      payloadDriverNeutronSim.operationResponseOut -> payloadManager.driverOperationResultIn
      payloadDriverNeutronSim.availabilityOut -> payloadManager.driverAvailabilityIn
      payloadManager.driverPassthroughOut -> payloadDriverNeutronSim.passthroughIn

      adcsManager.driverRequestOut -> adcsDriverD2S2.requestIn
      adcsDriverD2S2.statusOut -> adcsManager.driverStatusIn

      gpsManager.driverRequestOut -> gpsDriverArtemis.requestIn
      gpsDriverArtemis.statusOut -> gpsManager.driverStatusIn

      thermalManager.driverRequestOut -> thermalDriverArtemis.requestIn
      thermalDriverArtemis.statusOut -> thermalManager.driverStatusIn

    }

    connections SoHInputs {
      epsManager.sohStatusOut -> sohApp.statusIn[0]
      payloadManager.sohStatusOut -> sohApp.statusIn[1]
      adcsManager.sohStatusOut -> sohApp.statusIn[2]
      gpsManager.sohStatusOut -> sohApp.statusIn[3]
      storageManager.sohStatusOut -> sohApp.statusIn[4]
      thermalManager.sohStatusOut -> sohApp.statusIn[5]
      teensyTransportManager.sohStatusOut -> sohApp.statusIn[7]
    }

    connections ArtemisRpiTeensyDeployment {

    }

  }

}
