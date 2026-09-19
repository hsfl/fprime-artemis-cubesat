module PayloadComputerDeployment {

  # ----------------------------------------------------------------------
  # Symbolic constants for port numbers
  # ----------------------------------------------------------------------

  enum Ports_RateGroups {
    rateGroup_1Hz
    rateGroup_0_5Hz
    rateGroup_0_25Hz
  }

  deployment topology PayloadComputerDeployment {

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
    instance rateGroup_1Hz
    instance rateGroup_0_5Hz
    instance rateGroup_0_25Hz
    instance rateGroupDriver
    instance systemResources
    instance timer
    instance fcLinkDriver
    instance fcLinkManager
    instance fcLinkHub
    instance fcLinkAdapter
    instance fcLinkFramer
    instance fcLinkDeframer
    instance fcLinkComStub
    instance fcLinkAccumulator
    instance fcLinkBufferManager
    instance cmdSeq

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

    include "PayloadComputerDeploymentPackets.fppi"

  # ----------------------------------------------------------------------
  # Direct graph specifiers
  # ----------------------------------------------------------------------

    connections ComCcsds_CdhCore {
      # Core events and telemetry are NOT queued for downlink on this computer.
      #
      # /dev/serial0 belongs to the FC<->PC link, so ComCcsds.comStub has no
      # driver and comQueue has no outlet. Feeding it just overflows the queue
      # once per second. Events remain visible on the console through
      # CdhCore.textLogger, which is the intended window into this computer
      # during bring-up.
      #
      # The eventual fix is to point comQueue at a Svc.ComLogger and downlink
      # the resulting file over the hub, so this computer's full event and
      # telemetry stream is recoverable on the ground after the fact.
      #
      # CdhCore.events.PktSend -> ComCcsds.comQueue.comPacketQueueIn[ComCcsds.Ports_ComPacketQueue.EVENTS]
      # CdhCore.tlmSend.PktSend -> ComCcsds.comQueue.comPacketQueueIn[ComCcsds.Ports_ComPacketQueue.TELEMETRY]

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

    connections FcLink {
      # --- Heartbeat into the hub ---
      fcLinkManager.peerAliveOut        -> fcLinkHub.serialIn[FcPcLink.HEARTBEAT]

      # --- Downlink: hub -> framer -> ComStub -> UART ---
      fcLinkHub.allocate                -> fcLinkBufferManager.bufferGetCallee
      fcLinkHub.deallocate              -> fcLinkBufferManager.bufferSendIn
      fcLinkHub.toBufferDriver          -> fcLinkAdapter.bufferIn
      fcLinkAdapter.comDataOut          -> fcLinkFramer.dataIn
      fcLinkFramer.dataReturnOut        -> fcLinkAdapter.comDataReturnIn
      fcLinkAdapter.bufferInReturn      -> fcLinkHub.toBufferDriverReturn
      fcLinkFramer.bufferAllocate       -> fcLinkBufferManager.bufferGetCallee
      fcLinkFramer.bufferDeallocate     -> fcLinkBufferManager.bufferSendIn
      fcLinkFramer.dataOut              -> fcLinkComStub.dataIn
      fcLinkComStub.dataReturnOut       -> fcLinkFramer.dataReturnIn

      # ComStub invokes comStatusOut unconditionally and asserts if it is
      # unconnected. FprimeFramer guards its own comStatusOut, so the
      # backpressure chain terminates safely there.
      fcLinkComStub.comStatusOut           -> fcLinkFramer.comStatusIn

      # --- Uplink: UART -> accumulator -> deframer -> hub ---
      fcLinkComStub.dataOut             -> fcLinkAccumulator.dataIn
      fcLinkAccumulator.dataReturnOut   -> fcLinkComStub.dataReturnIn
      fcLinkAccumulator.bufferAllocate  -> fcLinkBufferManager.bufferGetCallee
      fcLinkAccumulator.bufferDeallocate -> fcLinkBufferManager.bufferSendIn
      fcLinkAccumulator.dataOut         -> fcLinkDeframer.dataIn
      fcLinkDeframer.dataReturnOut      -> fcLinkAccumulator.dataReturnIn
      fcLinkDeframer.dataOut            -> fcLinkAdapter.comDataIn
      fcLinkAdapter.bufferOut           -> fcLinkHub.fromBufferDriver
      fcLinkHub.fromBufferDriverReturn  -> fcLinkAdapter.bufferOutReturn
      fcLinkAdapter.comDataReturnOut    -> fcLinkDeframer.dataReturnIn

      # --- ComStub <-> PosixUartDriver ---
      fcLinkDriver.allocate             -> fcLinkBufferManager.bufferGetCallee
      fcLinkDriver.deallocate           -> fcLinkBufferManager.bufferSendIn
      fcLinkDriver.$recv                -> fcLinkComStub.drvReceiveIn
      fcLinkComStub.drvReceiveReturnOut -> fcLinkDriver.recvReturnIn
      fcLinkComStub.drvSendOut          -> fcLinkDriver.$send
      fcLinkDriver.ready                -> fcLinkComStub.drvConnected
    }

    connections FileHandling_DataProducts {
      # Data Products to File Downlink
      DataProducts.dpCat.fileOut -> FileHandling.fileDownlink.SendFile
      FileHandling.fileDownlink.FileComplete -> DataProducts.dpCat.fileDone
    }

    connections RateGroups {
      # timer to drive rate group
      timer.CycleOut -> rateGroupDriver.CycleIn

      # 1Hz rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup_1Hz] -> rateGroup_1Hz.CycleIn
      rateGroup_1Hz.RateGroupMemberOut[0] -> CdhCore.tlmSend.Run
      rateGroup_1Hz.RateGroupMemberOut[1] -> FileHandling.fileDownlink.Run
      rateGroup_1Hz.RateGroupMemberOut[2] -> systemResources.run
      rateGroup_1Hz.RateGroupMemberOut[3] -> ComCcsds.comQueue.run
      rateGroup_1Hz.RateGroupMemberOut[4] -> ComCcsds.aggregator.timeout
      rateGroup_1Hz.RateGroupMemberOut[5] -> CdhCore.cmdDisp.run
      rateGroup_1Hz.RateGroupMemberOut[6] -> fcLinkManager.run

      # 0.5Hz rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup_0_5Hz] -> rateGroup_0_5Hz.CycleIn
      rateGroup_0_5Hz.RateGroupMemberOut[0] -> cmdSeq.schedIn

      # 0.25Hz rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup_0_25Hz] -> rateGroup_0_25Hz.CycleIn
      rateGroup_0_25Hz.RateGroupMemberOut[0] -> CdhCore.$health.Run
      rateGroup_0_25Hz.RateGroupMemberOut[1] -> ComCcsds.commsBufferManager.schedIn
      rateGroup_0_25Hz.RateGroupMemberOut[2] -> DataProducts.dpBufferManager.schedIn
      rateGroup_0_25Hz.RateGroupMemberOut[3] -> DataProducts.dpWriter.schedIn
      rateGroup_0_25Hz.RateGroupMemberOut[4] -> DataProducts.dpMgr.schedIn
      rateGroup_0_25Hz.RateGroupMemberOut[5] -> fcLinkBufferManager.schedIn
    }

    connections CdhCore_cmdSeq {
      # Command Sequencer
      cmdSeq.comCmdOut -> CdhCore.cmdDisp.seqCmdBuff
      CdhCore.cmdDisp.seqCmdStatus -> cmdSeq.cmdResponseIn
    }

    connections PayloadComputerDeployment {

    }

  }

}
