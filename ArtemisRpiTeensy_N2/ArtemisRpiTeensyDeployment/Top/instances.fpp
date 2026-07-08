module ArtemisRpiTeensyDeployment {

  # ----------------------------------------------------------------------
  # Base ID Convention
  # ----------------------------------------------------------------------
  #
  # All Base IDs follow the 8-digit hex format: 0xDSSCCxxx
  #
  # Where:
  #   D   = Deployment digit (1 for this deployment)
  #   SS  = Subtopology digits (00 for main topology, 01-05 for subtopologies)
  #   CC  = Component digits (00, 01, 02, etc.)
  #   xxx = Reserved for internal component items (events, commands, telemetry)
  #

  # ----------------------------------------------------------------------
  # Defaults
  # ----------------------------------------------------------------------

  module Default {
    constant QUEUE_SIZE = 10
    constant STACK_SIZE = 64 * 1024
  }

  # ----------------------------------------------------------------------
  # Active component instances
  # ----------------------------------------------------------------------

  instance rateGroup1: Svc.ActiveRateGroup base id 0x10001000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 43

  instance rateGroup2: Svc.ActiveRateGroup base id 0x10002000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 42

  instance rateGroup3: Svc.ActiveRateGroup base id 0x10003000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 41

  instance cmdSeq: Svc.CmdSequencer base id 0x10004000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 40

  instance teensyTransportManager: Components.TeensyTransportManager base id 0x10005000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 39

  instance missionApp: Components.MissionApp base id 0x10006000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 38

  instance scienceApp: Components.ScienceApp base id 0x10007000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 37

  instance sohApp: Components.SoHApp base id 0x10008000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 36

  instance commsApp: Components.CommsApp base id 0x10009000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 35

  instance epsManager: Components.EpsManager base id 0x1000A000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 34

  instance payloadManager: Components.PayloadManager base id 0x1000B000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 33

  instance adcsManager: Components.AdcsManager base id 0x1000C000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 32

  instance gpsManager: Components.GpsManager base id 0x1000D000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 31

  instance storageManager: Components.StorageManager base id 0x1000E000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 30

  instance thermalManager: Components.ThermalManager base id 0x1000F000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 29

  instance payloadDownlinkApp: Components.PayloadDownlinkApp base id 0x10030000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 28

  instance payloadDriverLepton: Components.PayloadDriver_Lepton base id 0x10027000 \
    queue size Default.QUEUE_SIZE \
    stack size 256 * 1024 \
    priority 27

  instance payloadDriverNeutronSim: Components.PayloadDriver_NeutronSim base id 0x10021000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 26

  # ----------------------------------------------------------------------
  # Queued component instances
  # ----------------------------------------------------------------------


  # ----------------------------------------------------------------------
  # Passive component instances
  # ----------------------------------------------------------------------

  instance chronoTime: Svc.ChronoTime base id 0x10010000

  instance rateGroupDriver: Svc.RateGroupDriver base id 0x10011000

  instance systemResources: Svc.SystemResources base id 0x10012000

  instance timer: Svc.LinuxTimer base id 0x10013000

  instance comDriver: Drv.LinuxUartDriver base id 0x10014000

  instance epsDriverArtemis: Components.EpsDriver_Artemis base id 0x10020000

  instance adcsDriverD2S2: Components.AdcsDriver_D2S2 base id 0x10022000

  instance gpsDriverArtemis: Components.GpsDriver_Artemis base id 0x10023000

  instance commsDriverTeensyRfm23: Components.CommsDriver_TeensyRfm23 base id 0x10024000

  instance thermalDriverArtemis: Components.ThermalDriver_Artemis base id 0x10025000

  instance uartChannelMux: Components.UartChannelMux base id 0x10026000

  instance dpWrittenRouter: Components.DpWrittenRouter base id 0x10028000

}
