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
    instance teensyTransportService
    instance missionManager
    instance scienceManager
    instance sohManager
    instance commsManager
    instance epsService
    instance payloadService
    instance adcsService
    instance gpsService
    instance storageService
    instance thermalService
    instance payloadDownlinkManager
    instance epsAdapterArtemis
    instance payloadAdapterNeutronSim
    instance adcsAdapterD2S2
    instance gpsAdapterArtemis
    instance commsAdapterTeensyRfm23
    instance thermalAdapterArtemis
    instance uartChannelMux

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
      uartChannelMux.payloadRecvOut -> payloadDownlinkManager.packetIn
      payloadDownlinkManager.packetOut -> uartChannelMux.payloadSendIn
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
      rateGroup1.RateGroupMemberOut[5] -> teensyTransportService.run
      rateGroup1.RateGroupMemberOut[6] -> missionManager.run
      # Local emulation branch: run the minimum laptop demo-state path.
      rateGroup1.RateGroupMemberOut[7] -> scienceManager.run
      rateGroup1.RateGroupMemberOut[8] -> sohManager.run
      rateGroup1.RateGroupMemberOut[9] -> payloadDownlinkManager.run
      # Keep commsManager.run disabled here: it polls the adapter and can flood local GDS events.
      # rateGroup1.RateGroupMemberOut[10] -> commsManager.run

      # Rate group 2
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup2] -> rateGroup2.CycleIn
      rateGroup2.RateGroupMemberOut[0] -> cmdSeq.schedIn
      rateGroup2.RateGroupMemberOut[1] -> epsAdapterArtemis.run
      # RF/HIL subsystem polling stays off for laptop-only demo stability.
      rateGroup2.RateGroupMemberOut[2] -> payloadService.run
      # rateGroup2.RateGroupMemberOut[3] -> adcsService.run
      # rateGroup2.RateGroupMemberOut[4] -> gpsService.run
      rateGroup2.RateGroupMemberOut[5] -> storageService.run
      # rateGroup2.RateGroupMemberOut[6] -> thermalService.run

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
      missionManager.collectionRequestOut -> scienceManager.requestIn
      scienceManager.payloadRequestOut -> payloadService.requestIn
      payloadService.statusOut -> scienceManager.payloadStatusIn
      scienceManager.scienceProductOut -> storageService.requestIn
      storageService.downlinkReadyOut -> commsManager.scienceReadyIn
      commsManager.downlinkRequestOut -> storageService.downlinkRequestIn
      commsManager.payloadDownlinkRequestOut -> payloadDownlinkManager.downlinkRequestIn
      payloadDownlinkManager.statusOut -> commsManager.payloadDownlinkStatusIn
      scienceManager.missionModeOut -> missionManager.modeUpdateIn[0]
      commsManager.missionModeOut -> missionManager.modeUpdateIn[1]
    }

    connections ServiceAdapterBindings {
      epsService.adapterRequestOut -> epsAdapterArtemis.requestIn
      epsAdapterArtemis.statusOut -> epsService.adapterStatusIn
      epsAdapterArtemis.teensyRequestOut -> uartChannelMux.localSendIn
      uartChannelMux.localRecvOut -> epsAdapterArtemis.teensyResponseIn

      payloadService.adapterRequestOut -> payloadAdapterNeutronSim.requestIn
      payloadAdapterNeutronSim.statusOut -> payloadService.adapterStatusIn

      adcsService.adapterRequestOut -> adcsAdapterD2S2.requestIn
      adcsAdapterD2S2.statusOut -> adcsService.adapterStatusIn

      gpsService.adapterRequestOut -> gpsAdapterArtemis.requestIn
      gpsAdapterArtemis.statusOut -> gpsService.adapterStatusIn

      thermalService.adapterRequestOut -> thermalAdapterArtemis.requestIn
      thermalAdapterArtemis.statusOut -> thermalService.adapterStatusIn

      commsManager.adapterRequestOut -> commsAdapterTeensyRfm23.requestIn
      commsAdapterTeensyRfm23.teensyRequestOut -> uartChannelMux.rfLocalSendIn
      uartChannelMux.rfLocalRecvOut -> commsAdapterTeensyRfm23.teensyResponseIn
      commsAdapterTeensyRfm23.rssiStatusOut -> commsManager.rssiStatusIn
      commsAdapterTeensyRfm23.statusOut[0] -> commsManager.adapterStatusIn
      commsAdapterTeensyRfm23.statusOut[1] -> teensyTransportService.adapterStatusIn
    }

    connections TransportFlow {
      teensyTransportService.linkStatusOut -> commsManager.linkStatusIn
    }

    connections SoHInputs {
      epsService.sohStatusOut -> sohManager.statusIn[0]
      payloadService.sohStatusOut -> sohManager.statusIn[1]
      adcsService.sohStatusOut -> sohManager.statusIn[2]
      gpsService.sohStatusOut -> sohManager.statusIn[3]
      storageService.sohStatusOut -> sohManager.statusIn[4]
      thermalService.sohStatusOut -> sohManager.statusIn[5]
      commsManager.sohStatusOut -> sohManager.statusIn[6]
      teensyTransportService.sohStatusOut -> sohManager.statusIn[7]
    }

    connections ArtemisRpiTeensyDeployment {

    }

  }

}
