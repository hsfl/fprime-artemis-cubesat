module FprimeArtemisCore {

  # ----------------------------------------------------------------------
  # Symbolic constants for port numbers
  # ----------------------------------------------------------------------

  enum Ports_RateGroups {
    rateGroup_10Hz
    rateGroup_1Hz
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
    instance rateGroup_10Hz
    instance rateGroup_1Hz
    instance rateGroup_0_25Hz
    instance missionApp
    instance rateGroupDriver
    instance systemResources
    instance timer
    instance comDriver
    instance nullPrmDb
    instance rpiPowerManager
    instance rpiPowerDriver
    instance pcLinkManager
    instance pcLinkHub
    instance pcLinkAdapter
    instance pcLinkFramer
    instance pcLinkDeframer
    instance pcLinkComStub
    instance pcLinkDriver
    instance pcLinkAccumulator
    instance pcLinkBufferManager
    instance imuManager
    instance imuDriver
    instance imuI2cBus
    instance gpsManager
    instance gpsDriver
    instance gpsUartDriver
    instance gpsBufferManager
    instance thermalManager
    instance thermalDriver
    instance thermalAdcObc
    instance thermalAdcPdu
    instance thermalAdcBattery
    instance thermalAdcSolar1
    instance thermalAdcSolar2
    instance thermalAdcSolar3
    instance thermalAdcSolar4

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

      # 10Hz rate group: UART reads, plus thermal
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup_10Hz] -> rateGroup_10Hz.CycleIn
      rateGroup_10Hz.RateGroupMemberOut[0] -> comDriver.schedIn
      rateGroup_10Hz.RateGroupMemberOut[1] -> pcLinkDriver.schedIn
      rateGroup_10Hz.RateGroupMemberOut[2] -> gpsUartDriver.schedIn
      # No 2Hz group exists; thermalManager reads on every 5th tick (0.5 s).
      rateGroup_10Hz.RateGroupMemberOut[3] -> thermalManager.run

      # 1Hz rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup_1Hz] -> rateGroup_1Hz.CycleIn
      rateGroup_1Hz.RateGroupMemberOut[0] -> CdhCore.tlmSend.Run
      rateGroup_1Hz.RateGroupMemberOut[1] -> systemResources.run
      rateGroup_1Hz.RateGroupMemberOut[2] -> ComCcsds.comQueue.run
      rateGroup_1Hz.RateGroupMemberOut[3] -> ComCcsds.aggregator.timeout
      rateGroup_1Hz.RateGroupMemberOut[4] -> CdhCore.cmdDisp.run
      rateGroup_1Hz.RateGroupMemberOut[5] -> rpiPowerManager.run
      rateGroup_1Hz.RateGroupMemberOut[6] -> missionApp.run
      rateGroup_1Hz.RateGroupMemberOut[7] -> imuManager.run
      # The GPS emits GGA once a second: the driver ages its last sentence on
      # the same tick the manager reads it, so both run at 1Hz.
      rateGroup_1Hz.RateGroupMemberOut[8] -> gpsDriver.run
      rateGroup_1Hz.RateGroupMemberOut[9] -> gpsManager.run

      # 0.25Hz rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup_0_25Hz] -> rateGroup_0_25Hz.CycleIn
      rateGroup_0_25Hz.RateGroupMemberOut[0] -> CdhCore.$health.Run
      rateGroup_0_25Hz.RateGroupMemberOut[1] -> ComCcsds.commsBufferManager.schedIn
      rateGroup_0_25Hz.RateGroupMemberOut[2] -> pcLinkBufferManager.schedIn
      rateGroup_0_25Hz.RateGroupMemberOut[3] -> gpsBufferManager.schedIn
    }

    connections Mission {
      # Application tier drives managers through their port contracts.
      missionApp.rpiPowerRequestOut -> rpiPowerManager.powerRequestIn
      rpiPowerManager.stateOut      -> missionApp.rpiStateIn
    }

    connections RpiPower {
      # Manager tier drives the pin through the Drv.Gpio driver tier.
      # peerAliveIn is fed by the payload computer heartbeat; see connections PcLink.
      rpiPowerManager.gpioSet -> rpiPowerDriver.gpioWrite
    }

    connections Imu {
      # Manager tier drives the IMU through the driver's port contract
      # (Types/Imu.fpp); the driver reaches the chip through Drv.I2c.
      imuManager.driverPowerOut   -> imuDriver.powerRequestIn
      imuManager.driverReadingGet -> imuDriver.readingGet
      imuDriver.busWriteRead      -> imuI2cBus.writeRead
      imuDriver.busWrite          -> imuI2cBus.write
    }

    connections Gps {
      # Manager tier drives the GPS through the driver's port contract
      # (Types/Gps.fpp); the driver takes NMEA bytes off lpuart7.
      gpsManager.driverPowerOut   -> gpsDriver.powerRequestIn
      gpsManager.driverReadingGet -> gpsDriver.readingGet

      # --- ZephyrUartDriver <-> GPS driver ---
      gpsUartDriver.allocate      -> gpsBufferManager.bufferGetCallee
      gpsUartDriver.deallocate    -> gpsBufferManager.bufferSendIn
      gpsUartDriver.$recv         -> gpsDriver.drvReceiveIn
      gpsDriver.drvReceiveReturnOut -> gpsUartDriver.recvReturnIn
      gpsUartDriver.ready         -> gpsDriver.drvConnected

      # PMTK standby and wake sentences go out this way
      gpsDriver.drvSendOut        -> gpsUartDriver.$send
    }

    connections Thermal {
      # Manager tier reads temperatures through the driver's port contract
      # (Types/Thermal.fpp). The driver pulls one conversion from each ADC;
      # port index is the Components.ThermalSensor value. The ADCs' poll
      # ports stay unconnected: they only convert when the driver asks.
      thermalManager.driverReadingGet -> thermalDriver.readingGet

      thermalDriver.adcRead[Components.ThermalSensor.OBC] -> thermalAdcObc.readADC
      thermalAdcObc.adcMvValue -> thermalDriver.adcMvIn[Components.ThermalSensor.OBC]
      thermalDriver.adcRead[Components.ThermalSensor.PDU] -> thermalAdcPdu.readADC
      thermalAdcPdu.adcMvValue -> thermalDriver.adcMvIn[Components.ThermalSensor.PDU]
      thermalDriver.adcRead[Components.ThermalSensor.BATTERY] -> thermalAdcBattery.readADC
      thermalAdcBattery.adcMvValue -> thermalDriver.adcMvIn[Components.ThermalSensor.BATTERY]
      thermalDriver.adcRead[Components.ThermalSensor.SOLAR_1] -> thermalAdcSolar1.readADC
      thermalAdcSolar1.adcMvValue -> thermalDriver.adcMvIn[Components.ThermalSensor.SOLAR_1]
      thermalDriver.adcRead[Components.ThermalSensor.SOLAR_2] -> thermalAdcSolar2.readADC
      thermalAdcSolar2.adcMvValue -> thermalDriver.adcMvIn[Components.ThermalSensor.SOLAR_2]
      thermalDriver.adcRead[Components.ThermalSensor.SOLAR_3] -> thermalAdcSolar3.readADC
      thermalAdcSolar3.adcMvValue -> thermalDriver.adcMvIn[Components.ThermalSensor.SOLAR_3]
      thermalDriver.adcRead[Components.ThermalSensor.SOLAR_4] -> thermalAdcSolar4.readADC
      thermalAdcSolar4.adcMvValue -> thermalDriver.adcMvIn[Components.ThermalSensor.SOLAR_4]
    }

    connections PcLink {
      # --- Payload computer heartbeat, routed through the link manager ---
      pcLinkHub.serialOut[FcPcLink.HEARTBEAT] -> pcLinkManager.peerAliveIn
      pcLinkManager.peerAliveOut              -> rpiPowerManager.peerAliveIn

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
