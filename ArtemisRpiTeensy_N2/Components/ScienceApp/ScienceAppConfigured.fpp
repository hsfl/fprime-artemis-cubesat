module Components {
    @ Coordinates collection triggers and science-product handoff.
    active component ScienceApp {

        include "ScienceAppCommon.fppi"

        # Use a distinct opcode so packets from the timed-capture dictionary cannot trigger this command.
        @ Trigger one immediate collection using the configured capture duration.
        async command SCIENCE_CAPTURE opcode 7
    }
}
