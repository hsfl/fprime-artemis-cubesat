#ifndef SATELLITECONTROLLERDEPLOYMENT_TOPOLOGY_HPP
#define SATELLITECONTROLLERDEPLOYMENT_TOPOLOGY_HPP

#include <SatelliteControllerDeployment/Top/SatelliteControllerDeploymentTopologyDefs.hpp>

namespace SatelliteControllerDeployment {
void setupTopology(const TopologyState& state);
void teardownTopology(const TopologyState& state);
void startRateGroups();
void stopRateGroups();
}

#endif
