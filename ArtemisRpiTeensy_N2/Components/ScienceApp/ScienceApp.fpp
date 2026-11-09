module Components {
    @ Coordinates collection triggers and science-product handoff.
    active component ScienceApp {

        include "ScienceAppCommon.fppi"

        @ Trigger one immediate collection for the requested duration. The configured default stays in effect for later collections.
        async command SCIENCE_CAPTURE(durationMinutes: U32) opcode 2
    }
}
