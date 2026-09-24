telemetry packets FlightControllerDeploymentPackets {

  packet CDH id 1 group 1 {
    CdhCore.cmdDisp.CommandsDispatched
    CdhCore.cmdDisp.CommandsDropped
    CdhCore.events.EventsDropped
    #CdhCore.tlmSend.SendLevel # uncomment when using TlmPacketizer


    ComCcsds.comQueue.comQueueDepth
    ComCcsds.comQueue.buffQueueDepth
    CdhCore.tlmSend.GroupConfigs
    CdhCore.tlmSend.SectionEnabled
    ComCcsds.commsBufferManager.TotalBuffs
    ComCcsds.commsBufferManager.CurrBuffs
    ComCcsds.commsBufferManager.HiBuffs

    FprimeArtemisCore.rateGroup_10Hz.RgMaxTime
    FprimeArtemisCore.rateGroup_1Hz.RgMaxTime
    FprimeArtemisCore.rateGroup_0_25Hz.RgMaxTime
  }

  packet CDHErrors id 2 group 1 {
    CdhCore.$health.PingLateWarnings


    ComCcsds.commsBufferManager.NoBuffs
    ComCcsds.commsBufferManager.EmptyBuffs

    FprimeArtemisCore.rateGroup_10Hz.RgCycleSlips
    FprimeArtemisCore.rateGroup_1Hz.RgCycleSlips
    FprimeArtemisCore.rateGroup_0_25Hz.RgCycleSlips
  }

  packet Mission id 7 group 1 {
    FprimeArtemisCore.missionApp.CurrentMode
  }

  packet RpiPower id 3 group 1 {
    FprimeArtemisCore.rpiPowerManager.RpiState
    FprimeArtemisCore.rpiPowerManager.RpiPowerCycles
  }

  packet PcLink id 4 group 1 {
    FprimeArtemisCore.pcLinkManager.HeartbeatsReceived
    FprimeArtemisCore.pcLinkManager.LastHeartbeatKey
  }

  packet Imu id 8 group 1 {
    FprimeArtemisCore.imuManager.ImuState
    FprimeArtemisCore.imuManager.Acceleration
    FprimeArtemisCore.imuManager.AngularRate
    FprimeArtemisCore.imuManager.ImuTemperature
    FprimeArtemisCore.imuManager.ReadErrors
    FprimeArtemisCore.imuDriver.PowerState
  }

  packet Gps id 9 group 1 {
    FprimeArtemisCore.gpsManager.GpsState
    FprimeArtemisCore.gpsManager.GpsPowered
    FprimeArtemisCore.gpsManager.Latitude
    FprimeArtemisCore.gpsManager.Longitude
    FprimeArtemisCore.gpsManager.Altitude
    FprimeArtemisCore.gpsManager.Satellites
    FprimeArtemisCore.gpsManager.GpsUtcSeconds
    # Bring-up diagnostics live in this group 1 packet on purpose: the
    # packetizer starts at level 1, so a group 2 packet is disabled at boot and
    # would never reach the ground without a SET_LEVEL command.
    FprimeArtemisCore.gpsManager.FixLostCount
    FprimeArtemisCore.gpsDriver.SentencesParsed
    FprimeArtemisCore.gpsDriver.SentenceErrors
  }

  packet SystemRes1 id 5 group 2 {
    FprimeArtemisCore.systemResources.MEMORY_TOTAL
    FprimeArtemisCore.systemResources.MEMORY_USED
    FprimeArtemisCore.systemResources.NON_VOLATILE_TOTAL
    FprimeArtemisCore.systemResources.NON_VOLATILE_FREE
  }

  packet SystemRes3 id 6 group 2 {
    FprimeArtemisCore.systemResources.CPU
    FprimeArtemisCore.systemResources.CPU_00
    FprimeArtemisCore.systemResources.CPU_01
    FprimeArtemisCore.systemResources.CPU_02
    FprimeArtemisCore.systemResources.CPU_03
    FprimeArtemisCore.systemResources.CPU_04
    FprimeArtemisCore.systemResources.CPU_05
    FprimeArtemisCore.systemResources.CPU_06
    FprimeArtemisCore.systemResources.CPU_07
    FprimeArtemisCore.systemResources.CPU_08
    FprimeArtemisCore.systemResources.CPU_09
    FprimeArtemisCore.systemResources.CPU_10
    FprimeArtemisCore.systemResources.CPU_11
    FprimeArtemisCore.systemResources.CPU_12
    FprimeArtemisCore.systemResources.CPU_13
    FprimeArtemisCore.systemResources.CPU_14
    FprimeArtemisCore.systemResources.CPU_15
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
  # FC↔PC link buffer pool health: not downlinked during MVP bring-up
  FprimeArtemisCore.pcLinkBufferManager.TotalBuffs
  FprimeArtemisCore.pcLinkBufferManager.CurrBuffs
  FprimeArtemisCore.pcLinkBufferManager.HiBuffs
  FprimeArtemisCore.pcLinkBufferManager.NoBuffs
  FprimeArtemisCore.pcLinkBufferManager.EmptyBuffs
  # GPS UART buffer pool health: not downlinked during MVP bring-up
  FprimeArtemisCore.gpsBufferManager.TotalBuffs
  FprimeArtemisCore.gpsBufferManager.CurrBuffs
  FprimeArtemisCore.gpsBufferManager.HiBuffs
  FprimeArtemisCore.gpsBufferManager.NoBuffs
  FprimeArtemisCore.gpsBufferManager.EmptyBuffs
}
