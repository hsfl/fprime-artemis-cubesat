module SatelliteControllerDeployment {
    enum Ports_RateGroups {
        rateGroup
    }

    topology SatelliteControllerDeployment {
        instance rateGroup
        instance bridgeShell
        instance rateGroupDriver
        instance timer
        instance chronoTime

        time connections instance chronoTime

        connections RateGroups {
            timer.CycleOut -> rateGroupDriver.CycleIn
            rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup] -> rateGroup.CycleIn
            rateGroup.RateGroupMemberOut[0] -> bridgeShell.run
        }

        connections SatelliteControllerDeployment {
        }
    }
}
