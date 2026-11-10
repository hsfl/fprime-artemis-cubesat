module ArtemisRpiTeensyDeployment {
  topology ArtemisRpiTeensyDeployment {
    include "topology.cdr_tcp.shared.fppi"

    instance payloadManager
    instance payloadDriverNeutronSim

    connections SimulatorPayload {
      rateGroup2.RateGroupMemberOut[3] -> payloadDriverNeutronSim.run
      scienceApp.payloadRequestOut -> payloadManager.requestIn
      payloadManager.driverRequestOut -> payloadDriverNeutronSim.requestIn
      payloadDriverNeutronSim.operationResponseOut -> payloadManager.driverOperationResultIn
      payloadDriverNeutronSim.availabilityOut -> payloadManager.driverAvailabilityIn
      payloadManager.driverPassthroughOut -> payloadDriverNeutronSim.passthroughIn
    }
  }
}
