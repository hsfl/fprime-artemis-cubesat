// ======================================================================
// \title  FlightControllerDeploymentTopology.cpp
// \brief cpp file containing the topology instantiation code
//
// ======================================================================
// Provides access to autocoded functions
#include <FprimeArtemisCore/Deployments/FlightControllerDeployment/Top/FlightControllerDeploymentTopologyAc.hpp>
// Note: Uncomment when using Svc:TlmPacketizer
//#include <FprimeArtemisCore/Deployments/FlightControllerDeployment/Top/FlightControllerDeploymentPacketsAc.hpp>

// Necessary project-specified types
#include <Fw/Types/MallocAllocator.hpp>

// Public functions for use in main program are namespaced with deployment module FprimeArtemisCore
// This is also the namespace where the topology components are instantiated by FPP.
namespace FprimeArtemisCore {

// Instantiate a malloc allocator for cmdSeq buffer allocation
Fw::MallocAllocator mallocator;

// Allocator for the pcLinkHub link buffer manager and frame accumulator
Fw::MallocAllocator pcLinkAllocator;

// Rate group timing: base clock interval and divisors are coupled to rate group names
constexpr U32 BASE_RATEGROUP_PERIOD_MS = 1000;  // 1Hz base clock
Svc::RateGroupDriver::DividerSet rateGroupDivisorsSet{{{1, 0}, {2, 0}, {4, 0}}};
// Divisors: 1Hz, 0.5Hz, 0.25Hz

// Context tokens for rate group members (unused, set to zero)
Svc::ActiveRateGroup::ContextArray rateGroup_1HzContext(0);
Svc::ActiveRateGroup::ContextArray rateGroup_0_5HzContext(0);
Svc::ActiveRateGroup::ContextArray rateGroup_0_25HzContext(0);

/**
 * \brief configure/setup components in project-specific way
 *
 * This is a *helper* function which configures/sets up each component requiring project specific input. This includes
 * allocating resources, passing-in arguments, etc. This function may be inlined into the topology setup function if
 * desired, but is extracted here for clarity.
 */
void configureTopology() {
    // Rate group driver needs a divisor list
    rateGroupDriver.configure(rateGroupDivisorsSet);

    // Rate groups require context arrays.
    rateGroup_1Hz.configure(rateGroup_1HzContext);
    rateGroup_0_5Hz.configure(rateGroup_0_5HzContext);
    rateGroup_0_25Hz.configure(rateGroup_0_25HzContext);

    // Command sequencer needs to allocate memory to hold contents of command sequences
    cmdSeq.allocateBuffer(0, mallocator, 5 * 1024);
}

void setupTopology(const TopologyState& state) {
    // Autocoded initialization. Function provided by autocoder.
    initComponents(state);
    // Autocoded id setup. Function provided by autocoder.
    setBaseIds();
    // Autocoded connection wiring. Function provided by autocoder.
    connectComponents();
    // Autocoded command registration. Function provided by autocoder.
    regCommands();
    // Autocoded configuration. Function provided by autocoder.
    configComponents(state);
    // Project-specific component configuration. Function provided above. May be inlined, if desired.
    configureTopology();
    // Autocoded parameter read from file. Function provided by autocoder.
    readParameters();
    // Autocoded parameter loading. Function provided by autocoder.
    loadParameters();
    // Autocoded task kick-off (active components). Function provided by autocoder.
    startTasks(state);
    // Uplink is configured for receive; the Zephyr driver uses an interrupt callback,
    // so no separate receive task is started.
    comDriver.configure(state.uartDevice, state.baudRate);
    // FC↔PC link to the PayloadComputer over lpuart4
    pcLinkDriver.configure(state.pcLinkDevice, state.pcLinkBaud);

    // Payload computer power-enable pin (rpi_power node, Teensy pin 36)
    static const struct gpio_dt_spec rpiPowerPin = GPIO_DT_SPEC_GET(DT_NODELABEL(rpi_power), rpi_enable_gpios);
    (void)rpiPowerDriver.open(rpiPowerPin, Zephyr::ZephyrGpioDriver::GpioConfiguration::OUT);
}

void startRateGroups() {
    timer.configure(BASE_RATEGROUP_PERIOD_MS);
    timer.start();
    // Blocks forever: the Zephyr rate driver is cycled from this main loop
    while (1) {
        timer.cycle();
    }
}

void stopRateGroups() {
    timer.stop();
}

void teardownTopology(const TopologyState& state) {
    // Autocoded (active component) task clean-up. Functions provided by topology autocoder.
    stopTasks(state);
    freeThreads(state);

    // Resource deallocation
    cmdSeq.deallocateBuffer(mallocator);

    tearDownComponents(state);
    deinitComponents(state);
}
};  // namespace FprimeArtemisCore

namespace FprimeArtemisConfig {
const Svc::TlmPacketizerPacketList& tlmPacketList() {
    return FprimeArtemisCore::FlightControllerDeployment_FlightControllerDeploymentPacketsTlmPackets::packetList;
}
}  // namespace FprimeArtemisConfig
