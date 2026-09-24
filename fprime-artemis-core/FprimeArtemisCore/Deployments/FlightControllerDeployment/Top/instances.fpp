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

  # Rate groups run off a 10Hz base clock (100 ms). Zephyr priorities are
  # 0..14, lower = more urgent.
  #
  # The RateGroupDriver has exactly three outputs (RateGroupDriverRateGroupPorts
  # in the framework's AcConstants.fpp), so there are exactly three groups.

  # 10Hz rate group (divisor 1). UART reads only: ZephyrUartDriver reads at most
  # 64 bytes per schedIn, so 10Hz gives each link a 640 B/s read ceiling.
  instance rateGroup_10Hz: Svc.ActiveRateGroup base id 0x10001000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 1

  # 1Hz rate group (divisor 10). Anything with a timeout counted in ticks lives
  # here: RpiPowerManager::PEER_TIMEOUT_TICKS means seconds only at 1Hz.
  instance rateGroup_1Hz: Svc.ActiveRateGroup base id 0x10002000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 2

  # 0.25Hz rate group (divisor 40)
  instance rateGroup_0_25Hz: Svc.ActiveRateGroup base id 0x10003000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 3

  # Application tier: the mission operator's interface. Priority 6 sits
  # below the rate groups (1-3) and ComCcsds (4-5), above CdhCore (10-13).
  instance missionApp: Components.MissionApp base id 0x10004000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 6

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
  # FC<->PC link manager
  # ----------------------------------------------------------------------

  instance pcLinkManager: Components.PayloadComputerLinkManager base id 0x10018000

  # ----------------------------------------------------------------------
  # IMU (Adafruit LSM6DSOX+LIS3MDL breakout on lpi2c1, Teensy pins 18/19)
  # ----------------------------------------------------------------------
  #
  #   imuManager -> imuDriver -> imuI2cBus
  #
  # The manager is chip-independent; imuDriver knows the LSM6DSOX registers;
  # imuI2cBus only moves bytes. The address comes from the lsm6dsox devicetree
  # node, so the overlay is its single source.

  instance imuManager: Components.ImuManager base id 0x10028000

  instance imuDriver: Components.ImuDriver_LSM6DSOX base id 0x10029000 \
    {
      phase Fpp.ToCpp.Phases.configComponents """
      FprimeArtemisCore::imuDriver.configure(DT_REG_ADDR(DT_NODELABEL(lsm6dsox)));
      """
    }

  instance imuI2cBus: Zephyr.ZephyrI2cDriver base id 0x1002A000 \
    {
      phase Fpp.ToCpp.Phases.configComponents """
      if (FprimeArtemisCore::imuI2cBus.open(DEVICE_DT_GET(DT_NODELABEL(lpi2c1))) != Drv::I2cStatus::I2C_OK) {
          Fw::Logger::log("[ERROR] IMU I2C bus (lpi2c1) not ready\\n");
      }
      """
    }

  # ----------------------------------------------------------------------
  # GPS (Adafruit Mini GPS PA1010D on lpuart7, Teensy pins 28/29, 9600 baud)
  # ----------------------------------------------------------------------
  #
  #   gpsManager -> gpsDriver <- gpsUartDriver -> gpsBufferManager
  #
  # The manager is module-independent; gpsDriver knows NMEA; gpsUartDriver only
  # moves bytes. The module has no enable line, so nothing commands it on or
  # off: it talks whenever it has power, and silence is how "off" is detected.

  instance gpsManager: Components.GpsManager base id 0x1002B000

  instance gpsDriver: Components.GpsDriver_AdafruitMiniGps base id 0x1002C000

  instance gpsUartDriver: Zephyr.ZephyrUartDriver base id 0x1002D000

  instance gpsBufferManager: Svc.BufferManager base id 0x1002E000 \
    {
      phase Fpp.ToCpp.Phases.configObjects """
      Svc::BufferManager::BufferBins bins;
      """
      phase Fpp.ToCpp.Phases.configComponents """
      memset(&ConfigObjects::FprimeArtemisCore_gpsBufferManager::bins, 0,
             sizeof(ConfigObjects::FprimeArtemisCore_gpsBufferManager::bins));
      ConfigObjects::FprimeArtemisCore_gpsBufferManager::bins.bins[0].bufferSize = Gps::bufferSize;
      ConfigObjects::FprimeArtemisCore_gpsBufferManager::bins.bins[0].numBuffers = Gps::bufferCount;
      FprimeArtemisCore::gpsBufferManager.setup(
          Gps::bufferManagerId,
          0,
          FprimeArtemisCore::gpsAllocator,
          ConfigObjects::FprimeArtemisCore_gpsBufferManager::bins
      );
      """
      phase Fpp.ToCpp.Phases.tearDownComponents """
      FprimeArtemisCore::gpsBufferManager.cleanup();
      """
    }

  # ----------------------------------------------------------------------
  # FC↔PC link to the PayloadComputer (Raspberry Pi) over lpuart4
  # ----------------------------------------------------------------------
  #
  #   pcLinkHub <-> pcLinkAdapter <-> pcLinkFramer/pcLinkDeframer <-> pcLinkComStub <-> pcLinkDriver
  #
  # The framing chain is required because a UART has no message boundaries and
  # GenericHub drops any message whose declared size does not match the buffer
  # it receives.

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
