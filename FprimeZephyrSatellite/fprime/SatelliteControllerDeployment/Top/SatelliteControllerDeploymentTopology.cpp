#include <SatelliteControllerDeployment/Top/SatelliteControllerDeploymentTopologyAc.hpp>
#include <SatelliteControllerDeployment/Top/SatelliteControllerDeploymentTopology.hpp>

#include <Fw/Types/Assert.hpp>

using namespace SatelliteControllerDeployment;

namespace {
// Poll faster than the 80 ms RF ACK timeout and preserve the 8/15 ms RF
// pacing contracts. The rate-group queue is sized for the bounded 1 s
// two-attempt radio timeout path.
constexpr FwSizeType BASE_RATE_GROUP_PERIOD_MS = 5;

Svc::RateGroupDriver::DividerSet rateGroupDividers{{
    {1, 0},
}};

U32 rateGroupContext[Svc::ActiveRateGroup::CONNECTION_COUNT_MAX] = {};
}

void configureTopology() {
    rateGroupDriver.configure(rateGroupDividers);
    rateGroup.configure(rateGroupContext, FW_NUM_ARRAY_ELEMENTS(rateGroupContext));
}

namespace SatelliteControllerDeployment {

void setupTopology(const TopologyState& state) {
    initComponents(state);
    setBaseIds();
    connectComponents();
    regCommands();
    configComponents(state);
    configureTopology();
    loadParameters();
    startTasks(state);
}

void startRateGroups() {
    timer.configure(BASE_RATE_GROUP_PERIOD_MS);
    timer.start();
    while (true) {
        timer.cycle();
    }
}

void stopRateGroups() {
    timer.stop();
}

void teardownTopology(const TopologyState& state) {
    stopRateGroups();
    stopTasks(state);
    freeThreads(state);
    tearDownComponents(state);
}

}  // namespace SatelliteControllerDeployment
