#include <Os/Os.hpp>
#include <SatelliteController/ControllerApp.hpp>

#include <SatelliteControllerDeployment/Top/SatelliteControllerDeploymentTopology.hpp>

int main() {
    Os::init();
    (void)SatelliteController::controllerAppInitialize();

    static SatelliteControllerDeployment::TopologyState state{};
    SatelliteControllerDeployment::setupTopology(state);
    SatelliteControllerDeployment::startRateGroups();
    SatelliteControllerDeployment::teardownTopology(state);
    return 0;
}
