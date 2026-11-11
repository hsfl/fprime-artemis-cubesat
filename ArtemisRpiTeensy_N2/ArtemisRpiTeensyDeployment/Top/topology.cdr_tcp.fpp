module ArtemisRpiTeensyDeployment {
  topology ArtemisRpiTeensyDeployment {
    include "topology.cdr_tcp.shared.fppi"

    instance payloadManager
    instance payloadDriverNeutronSim

    connections SimulatorPayload {
      rateGroup2.RateGroupMemberOut[3] -> payloadDriverNeutronSim.run
      scienceApp.payloadRequestOut -> payloadManager.requestIn
      # MissionApp's capture gate. It needs this PayloadManager's control ports, so it stays out of the shared wiring.
      missionApp.captureControlOut -> payloadManager.captureControlIn
      payloadManager.captureControlReportOut -> missionApp.captureControlReportIn
      payloadManager.driverRequestOut -> payloadDriverNeutronSim.requestIn
      payloadDriverNeutronSim.operationResponseOut -> payloadManager.driverOperationResultIn
      payloadDriverNeutronSim.availabilityOut -> payloadManager.driverAvailabilityIn
      payloadManager.driverPassthroughOut -> payloadDriverNeutronSim.passthroughIn
    }
  }
}
