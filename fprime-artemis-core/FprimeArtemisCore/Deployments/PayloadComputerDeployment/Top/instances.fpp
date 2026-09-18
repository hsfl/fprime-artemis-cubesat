module PayloadComputerDeployment {

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

  # 1Hz rate group (divisor 1 of 1Hz base clock)
  instance rateGroup_1Hz: Svc.ActiveRateGroup base id 0x10001000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 43

  # 0.5Hz rate group (divisor 2 of 1Hz base clock)
  instance rateGroup_0_5Hz: Svc.ActiveRateGroup base id 0x10002000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 42

  # 0.25Hz rate group (divisor 4 of 1Hz base clock)
  instance rateGroup_0_25Hz: Svc.ActiveRateGroup base id 0x10003000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 41

  instance cmdSeq: Svc.CmdSequencer base id 0x10004000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 40

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

  # ----------------------------------------------------------------------
  # FC<->PC link to the FlightController (Teensy 4.1) over /dev/serial0
  # ----------------------------------------------------------------------
  #
  # This is the payload computer's only serial path: its USB port carries the
  # payload camera, so the link to the flight controller is also its only route
  # to the ground. See docs/N2_DUAL_DEPLOYMENT_REFACTOR_PLAN_2026-09-17.md.

  instance fcLinkDriver: Drv.PosixUartDriver base id 0x10014000

  instance fcLinkManager: Components.FlightControllerLinkManager base id 0x10016000

  instance fcLinkHub: Svc.GenericHub base id 0x10020000

  instance fcLinkAdapter: Components.ComBufferAdapter base id 0x10021000

  instance fcLinkFramer: Svc.FprimeFramer base id 0x10022000

  instance fcLinkDeframer: Svc.FprimeDeframer base id 0x10023000

  instance fcLinkComStub: Svc.ComStub base id 0x10024000

  instance fcLinkAccumulator: Svc.FrameAccumulator base id 0x10026000 \
    {
      phase Fpp.ToCpp.Phases.configObjects """
      Svc::FrameDetectors::FprimeFrameDetector frameDetector;
      """
      phase Fpp.ToCpp.Phases.configComponents """
      PayloadComputerDeployment::fcLinkAccumulator.configure(
          ConfigObjects::PayloadComputerDeployment_fcLinkAccumulator::frameDetector,
          1,
          PayloadComputerDeployment::fcLinkAllocator,
          FcLink::accumulatorSize
      );
      """
      phase Fpp.ToCpp.Phases.tearDownComponents """
      PayloadComputerDeployment::fcLinkAccumulator.cleanup();
      """
    }

  instance fcLinkBufferManager: Svc.BufferManager base id 0x10027000 \
    {
      phase Fpp.ToCpp.Phases.configObjects """
      Svc::BufferManager::BufferBins bins;
      """
      phase Fpp.ToCpp.Phases.configComponents """
      memset(&ConfigObjects::PayloadComputerDeployment_fcLinkBufferManager::bins, 0,
             sizeof(ConfigObjects::PayloadComputerDeployment_fcLinkBufferManager::bins));
      ConfigObjects::PayloadComputerDeployment_fcLinkBufferManager::bins.bins[0].bufferSize = FcLink::bufferSize;
      ConfigObjects::PayloadComputerDeployment_fcLinkBufferManager::bins.bins[0].numBuffers = FcLink::bufferCount;
      PayloadComputerDeployment::fcLinkBufferManager.setup(
          FcLink::bufferManagerId,
          0,
          PayloadComputerDeployment::fcLinkAllocator,
          ConfigObjects::PayloadComputerDeployment_fcLinkBufferManager::bins
      );
      """
      phase Fpp.ToCpp.Phases.tearDownComponents """
      PayloadComputerDeployment::fcLinkBufferManager.cleanup();
      """
    }

}
