module Components {
    @ Radio-agnostic link constants for the channelized RF bridge.
    module LinkCfg {
        constant UART_FRAME_MAX_PAYLOAD = 220
        constant UART_FRAME_OVERHEAD = 7
        constant RF_PACKET_MAX_BYTES = 49
        constant RF_SEGMENT_HEADER_BYTES = 5
        constant RF_SEGMENT_MAX_DATA_BYTES = 44
        constant CHANNEL_CCSDS = 0
        constant CHANNEL_PAYLOAD = 1
        constant PAYLOAD_PACKET_MAX_BYTES = 44
        constant PAYLOAD_PACKET_DATA_BYTES = 35
    }
}
