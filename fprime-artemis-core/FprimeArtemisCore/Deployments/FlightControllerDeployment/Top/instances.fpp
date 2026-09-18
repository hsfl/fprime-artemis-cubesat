module FprimeArtemisCore {

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
    constant STACK_SIZE = 8 * 1024 # Must match prj.conf CONFIG_DYNAMIC_THREAD_STACK_SIZE
  }

  # ----------------------------------------------------------------------
  # Active component instances
  # ----------------------------------------------------------------------

  # 1Hz rate group (divisor 1 of 1Hz base clock)
  instance rateGroup_1Hz: Svc.ActiveRateGroup base id 0x10001000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 1  # Zephyr: 0..14, lower = more urgent. Drives pcLinkDriver/comDriver reads

  # 0.5Hz rate group (divisor 2 of 1Hz base clock)
  instance rateGroup_0_5Hz: Svc.ActiveRateGroup base id 0x10002000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 2

  # 0.25Hz rate group (divisor 4 of 1Hz base clock)
  instance rateGroup_0_25Hz: Svc.ActiveRateGroup base id 0x10003000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 3

  instance cmdSeq: Svc.CmdSequencer base id 0x10004000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 14 # least urgent: cmdSeq cannot load sequences without a filesystem

  # ----------------------------------------------------------------------
  # Queued component instances
  # ----------------------------------------------------------------------


  # ----------------------------------------------------------------------
  # Passive component instances
  # ----------------------------------------------------------------------

  instance chronoTime: Zephyr.ZephyrTime base id 0x10010000

  instance rateGroupDriver: Svc.RateGroupDriver base id 0x10011000

  instance systemResources: Svc.SystemResources base id 0x10012000

  instance timer: Zephyr.ZephyrRateDriver base id 0x10013000

  instance comDriver: Zephyr.ZephyrUartDriver base id 0x10014000

  instance nullPrmDb: Components.NullPrmDb base id 0x10015000

  # ----------------------------------------------------------------------
  # Payload computer power control (pin 36, rpi_power node)
  # ----------------------------------------------------------------------

  instance rpiPowerManager: Components.RpiPowerManager base id 0x10016000

  instance rpiPowerDriver: Zephyr.ZephyrGpioDriver base id 0x10017000

  # ----------------------------------------------------------------------
  # FC↔PC link to the PayloadComputer (Raspberry Pi) over lpuart4
  # ----------------------------------------------------------------------
  #
  #   pcLinkHub <-> pcLinkAdapter <-> pcLinkFramer/pcLinkDeframer <-> pcLinkComStub <-> pcLinkDriver
  #
  # The framing chain is required because a UART has no message boundaries and
  # GenericHub drops any message whose declared size does not match the buffer
  # it receives. See docs/HUB_UART_DEPLOYMENT_LINK_PLAN_2026-09-16.md.

  instance pcLinkHub: Svc.GenericHub base id 0x10020000

  instance pcLinkAdapter: Components.ComBufferAdapter base id 0x10021000

  instance pcLinkFramer: Svc.FprimeFramer base id 0x10022000

  instance pcLinkDeframer: Svc.FprimeDeframer base id 0x10023000

  instance pcLinkComStub: Svc.ComStub base id 0x10024000

  instance pcLinkDriver: Zephyr.ZephyrUartDriver base id 0x10025000

  instance pcLinkAccumulator: Svc.FrameAccumulator base id 0x10026000 \
    {
      phase Fpp.ToCpp.Phases.configObjects """
      Svc::FrameDetectors::FprimeFrameDetector frameDetector;
      """
      phase Fpp.ToCpp.Phases.configComponents """
      FprimeArtemisCore::pcLinkAccumulator.configure(
          ConfigObjects::FprimeArtemisCore_pcLinkAccumulator::frameDetector,
          1,
          FprimeArtemisCore::pcLinkAllocator,
          PcLink::accumulatorSize
      );
      """
      phase Fpp.ToCpp.Phases.tearDownComponents """
      FprimeArtemisCore::pcLinkAccumulator.cleanup();
      """
    }

  instance pcLinkBufferManager: Svc.BufferManager base id 0x10027000 \
    {
      phase Fpp.ToCpp.Phases.configObjects """
      Svc::BufferManager::BufferBins bins;
      """
      phase Fpp.ToCpp.Phases.configComponents """
      memset(&ConfigObjects::FprimeArtemisCore_pcLinkBufferManager::bins, 0,
             sizeof(ConfigObjects::FprimeArtemisCore_pcLinkBufferManager::bins));
      ConfigObjects::FprimeArtemisCore_pcLinkBufferManager::bins.bins[0].bufferSize = PcLink::bufferSize;
      ConfigObjects::FprimeArtemisCore_pcLinkBufferManager::bins.bins[0].numBuffers = PcLink::bufferCount;
      FprimeArtemisCore::pcLinkBufferManager.setup(
          PcLink::bufferManagerId,
          0,
          FprimeArtemisCore::pcLinkAllocator,
          ConfigObjects::FprimeArtemisCore_pcLinkBufferManager::bins
      );
      """
      phase Fpp.ToCpp.Phases.tearDownComponents """
      FprimeArtemisCore::pcLinkBufferManager.cleanup();
      """
    }

}
