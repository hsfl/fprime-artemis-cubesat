module FprimeArtemisCore {

  # ----------------------------------------------------------------------
  # Symbolic constants for port numbers
  # ----------------------------------------------------------------------

  enum Ports_RateGroups {
    rateGroup_1Hz
    rateGroup_0_5Hz
    rateGroup_0_25Hz
  }

  deployment topology FlightControllerDeployment {

  # ----------------------------------------------------------------------
  # Subtopology imports
  # ----------------------------------------------------------------------
    import CdhCore.Subtopology
    import ComCcsds.Subtopology
    
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
    instance comDriver
    instance cmdSeq
    instance nullPrmDb
    instance rpiPowerManager
    instance rpiPowerDriver
    instance pcLinkHub
    instance pcLinkAdapter
    instance pcLinkFramer
    instance pcLinkDeframer
    instance pcLinkComStub
    instance pcLinkDriver
    instance pcLinkAccumulator
    instance pcLinkBufferManager

  # ----------------------------------------------------------------------
  # Pattern graph specifiers
  # ----------------------------------------------------------------------

    command connections instance CdhCore.cmdDisp
    event connections instance CdhCore.events
    telemetry connections instance CdhCore.tlmSend
    text event connections instance CdhCore.textLogger
    health connections instance CdhCore.$health
    param connections instance nullPrmDb
    time connections instance chronoTime

  # ----------------------------------------------------------------------
  # Telemetry packets (only used when TlmPacketizer is used)
  # ----------------------------------------------------------------------

    include "FlightControllerDeploymentPackets.fppi"

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


    connections Communications {
      # ComDriver buffer allocations
      comDriver.allocate      -> ComCcsds.commsBufferManager.bufferGetCallee
      comDriver.deallocate    -> ComCcsds.commsBufferManager.bufferSendIn
      
      # ComDriver <-> ComStub (Uplink)
      comDriver.$recv                     -> ComCcsds.comStub.drvReceiveIn
      ComCcsds.comStub.drvReceiveReturnOut -> comDriver.recvReturnIn
      
      # ComStub <-> ComDriver (Downlink)
      ComCcsds.comStub.drvSendOut      -> comDriver.$send
      comDriver.ready         -> ComCcsds.comStub.drvConnected
    }


    connections RateGroups {
      # timer to drive rate group
      timer.CycleOut -> rateGroupDriver.CycleIn

      # 1Hz rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup_1Hz] -> rateGroup_1Hz.CycleIn
      rateGroup_1Hz.RateGroupMemberOut[0] -> CdhCore.tlmSend.Run
      rateGroup_1Hz.RateGroupMemberOut[1] -> systemResources.run
      rateGroup_1Hz.RateGroupMemberOut[2] -> ComCcsds.comQueue.run
      rateGroup_1Hz.RateGroupMemberOut[3] -> ComCcsds.aggregator.timeout
      rateGroup_1Hz.RateGroupMemberOut[4] -> CdhCore.cmdDisp.run
      rateGroup_1Hz.RateGroupMemberOut[5] -> comDriver.schedIn
      rateGroup_1Hz.RateGroupMemberOut[6] -> pcLinkDriver.schedIn
      rateGroup_1Hz.RateGroupMemberOut[7] -> rpiPowerManager.run

      # 0.5Hz rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup_0_5Hz] -> rateGroup_0_5Hz.CycleIn
      rateGroup_0_5Hz.RateGroupMemberOut[0] -> cmdSeq.schedIn

      # 0.25Hz rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup_0_25Hz] -> rateGroup_0_25Hz.CycleIn
      rateGroup_0_25Hz.RateGroupMemberOut[0] -> CdhCore.$health.Run
      rateGroup_0_25Hz.RateGroupMemberOut[1] -> ComCcsds.commsBufferManager.schedIn
      rateGroup_0_25Hz.RateGroupMemberOut[2] -> pcLinkBufferManager.schedIn
    }

    connections CdhCore_cmdSeq {
      # Command Sequencer
      cmdSeq.comCmdOut -> CdhCore.cmdDisp.seqCmdBuff
      CdhCore.cmdDisp.seqCmdStatus -> cmdSeq.cmdResponseIn
    }

    connections RpiPower {
      # Manager tier drives the pin through the Drv.Gpio driver tier.
      # peerAliveIn is fed by the payload computer heartbeat; see connections PcLink.
      rpiPowerManager.gpioSet -> rpiPowerDriver.gpioWrite
    }

    connections PcLink {
      # --- Payload computer heartbeat (serial port 0 must match the peer) ---
      pcLinkHub.serialOut[0]            -> rpiPowerManager.peerAliveIn

      # --- Downlink: pcLinkHub -> framer -> ComStub -> UART ---
      pcLinkHub.allocate                      -> pcLinkBufferManager.bufferGetCallee
      pcLinkHub.deallocate                    -> pcLinkBufferManager.bufferSendIn
      pcLinkHub.toBufferDriver                -> pcLinkAdapter.bufferIn
      pcLinkAdapter.comDataOut         -> pcLinkFramer.dataIn
      pcLinkFramer.dataReturnOut           -> pcLinkAdapter.comDataReturnIn
      pcLinkAdapter.bufferInReturn     -> pcLinkHub.toBufferDriverReturn
      pcLinkFramer.bufferAllocate          -> pcLinkBufferManager.bufferGetCallee
      pcLinkFramer.bufferDeallocate        -> pcLinkBufferManager.bufferSendIn
      pcLinkFramer.dataOut                 -> pcLinkComStub.dataIn
      pcLinkComStub.dataReturnOut          -> pcLinkFramer.dataReturnIn

      # ComStub invokes comStatusOut unconditionally and asserts if it is
      # unconnected. FprimeFramer guards its own comStatusOut, so the
      # backpressure chain terminates safely there.
      pcLinkComStub.comStatusOut           -> pcLinkFramer.comStatusIn

      # --- Uplink: UART -> accumulator -> deframer -> pcLinkHub ---
      pcLinkComStub.dataOut                -> pcLinkAccumulator.dataIn
      pcLinkAccumulator.dataReturnOut      -> pcLinkComStub.dataReturnIn
      pcLinkAccumulator.bufferAllocate     -> pcLinkBufferManager.bufferGetCallee
      pcLinkAccumulator.bufferDeallocate   -> pcLinkBufferManager.bufferSendIn
      pcLinkAccumulator.dataOut            -> pcLinkDeframer.dataIn
      pcLinkDeframer.dataReturnOut         -> pcLinkAccumulator.dataReturnIn
      pcLinkDeframer.dataOut               -> pcLinkAdapter.comDataIn
      pcLinkAdapter.bufferOut          -> pcLinkHub.fromBufferDriver
      pcLinkHub.fromBufferDriverReturn        -> pcLinkAdapter.bufferOutReturn
      pcLinkAdapter.comDataReturnOut   -> pcLinkDeframer.dataReturnIn

      # --- ComStub <-> ZephyrUartDriver ---
      pcLinkDriver.allocate            -> pcLinkBufferManager.bufferGetCallee
      pcLinkDriver.deallocate          -> pcLinkBufferManager.bufferSendIn
      pcLinkDriver.$recv               -> pcLinkComStub.drvReceiveIn
      pcLinkComStub.drvReceiveReturnOut    -> pcLinkDriver.recvReturnIn
      pcLinkComStub.drvSendOut             -> pcLinkDriver.$send
      pcLinkDriver.ready               -> pcLinkComStub.drvConnected
    }

    connections FlightControllerDeployment {

    }

  }

}
