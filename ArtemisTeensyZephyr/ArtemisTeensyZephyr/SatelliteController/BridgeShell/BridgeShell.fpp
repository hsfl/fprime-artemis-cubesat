module SatelliteController {
    @ Minimal active bridge shell for the Teensy controller.
    @ It intentionally contains no Pi/CDH mission services.
    passive component BridgeShell {
        @ Rate-group tick used to run the controller bridge.
        sync input port run: Svc.Sched

        @ Number of rate-group ticks observed by the bridge.
        telemetry TickCount: U32

        @ Number of valid F' UART frames received.
        telemetry UartFramesReceived: U32

        @ Number of invalid or incomplete RF segments dropped.
        telemetry RadioFramesDropped: U32

        @ Portable radio state-machine state and fault snapshot.
        telemetry RadioState: U32
        telemetry RadioFault: U32
        telemetry RadioInitAttempts: U32

        @ Framework time source required by telemetry channels.
        time get port timeCaller

        import Fw.Channel
    }
}
