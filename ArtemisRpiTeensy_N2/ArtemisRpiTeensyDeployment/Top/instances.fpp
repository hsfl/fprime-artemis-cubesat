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

  instance teensyTransportService: Components.TeensyTransportService base id 0x10005000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 39

  instance missionManager: Components.MissionManager base id 0x10006000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 38

  instance scienceManager: Components.ScienceManager base id 0x10007000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 37

  instance sohManager: Components.SoHManager base id 0x10008000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 36

  instance commsManager: Components.CommsManager base id 0x10009000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 35

  instance epsService: Components.EpsService base id 0x1000A000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 34

  instance payloadService: Components.PayloadService base id 0x1000B000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 33

  instance adcsService: Components.AdcsService base id 0x1000C000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 32

  instance gpsService: Components.GpsService base id 0x1000D000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 31

  instance storageService: Components.StorageService base id 0x1000E000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 30

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

  instance epsAdapterArtemis: Components.EpsAdapter_Artemis base id 0x10020000

  instance payloadAdapterN1Legacy: Components.PayloadAdapter_N1Legacy base id 0x10021000

  instance adcsAdapterD2S2: Components.AdcsAdapter_D2S2 base id 0x10022000

  instance gpsAdapterArtemis: Components.GpsAdapter_Artemis base id 0x10023000

  instance commsAdapterTeensyRfm23: Components.CommsAdapter_TeensyRfm23 base id 0x10024000

}
