module CdhCoreConfig {
    constant BASE_ID = 0x01000000

    module QueueSizes {
        constant cmdDisp = 10
        @ Retain short diagnostic/command bursts before the event task drains.
        @ The stock ten-entry queue can drop replies before packetization.
        constant events = 128
        constant tlmSend = 10
        constant $health = 25
    }

    module StackSizes {
        constant cmdDisp = 64 * 1024
        constant events = 64 * 1024
        constant tlmSend = 64 * 1024
    }

    module Priorities {
        constant cmdDisp = 35
        constant $health = 24
        constant events = 23
        constant tlmSend = 22
    }
}
