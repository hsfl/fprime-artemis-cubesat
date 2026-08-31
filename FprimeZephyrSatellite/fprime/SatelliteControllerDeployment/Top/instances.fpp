module SatelliteControllerDeployment {
    module Default {
        constant QUEUE_SIZE = 8
        constant STACK_SIZE = 4096
    }

    instance rateGroup: Svc.ActiveRateGroup base id 0x10001000 \
        queue size 256 \
        stack size Default.STACK_SIZE \
        priority 3

    instance bridgeShell: SatelliteController.BridgeShell base id 0x10002000

    instance rateGroupDriver: Svc.RateGroupDriver base id 0x10003000
    instance timer: Zephyr.ZephyrRateDriver base id 0x10004000
    instance chronoTime: Zephyr.ZephyrTime base id 0x10005000
}
