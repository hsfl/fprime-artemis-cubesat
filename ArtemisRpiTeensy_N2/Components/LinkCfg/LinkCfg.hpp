#ifndef Components_LinkCfg_HPP
#define Components_LinkCfg_HPP

// Generated from config/transport_constants.json and config/rf_networks.json by tools/generate_transport_constants.py.
// Do not hand-edit constants here; update the manifest and regenerate.

#include <Fw/FPrimeBasicTypes.hpp>

namespace Components {
namespace LinkCfg {

static constexpr U8 CHANNEL_CCSDS = 0;
static constexpr U8 CHANNEL_PAYLOAD = 1;
static constexpr U8 CHANNEL_TEENSY_LOCAL = 2;
static constexpr U8 CHANNEL_COUNT = 3;

static constexpr U8 TEENSY_TARGET_PDU = 1;
static constexpr U8 TEENSY_TARGET_RF_STATUS = 2;
static constexpr U8 TEENSY_STATUS_OK = 0;
static constexpr U8 TEENSY_STATUS_BAD_REQUEST = 1;
static constexpr U8 TEENSY_STATUS_BUSY = 2;
static constexpr U8 TEENSY_STATUS_TIMEOUT = 3;
static constexpr U8 TEENSY_STATUS_TARGET_ERROR = 4;
static constexpr U8 TEENSY_RF_OP_LINK_STATS = 1;

static constexpr U8 UART_FRAME_MAGIC_0 = 0xD4;
static constexpr U8 UART_FRAME_MAGIC_1 = 0xC3;
static constexpr FwSizeType UART_FRAME_MAX_PAYLOAD = 220;
static constexpr U32 UART_BAUD = 115200;
static constexpr U32 UART_INTER_FRAME_MARGIN_US = 37000;
static constexpr U32 UART_CCSDS_EXTRA_MARGIN_US = 40000;
static constexpr FwSizeType UART_FRAME_OVERHEAD = 7;
static constexpr FwSizeType UART_FRAME_MAX_ENCODED =
    UART_FRAME_MAX_PAYLOAD + UART_FRAME_OVERHEAD;

static constexpr FwSizeType RF_PACKET_MAX_LEN = 49;
static constexpr U8 RF_NETWORK_ID = 0xC3;
static constexpr U8 RF_PROTOCOL_VERSION = 0x01;
static constexpr U8 RF_GROUND_ADDRESS = 0xA1;
static constexpr U8 RF_SATELLITE_ADDRESS = 0xA2;
static constexpr FwSizeType RF_SEGMENT_HEADER_LEN = 5;
static constexpr FwSizeType RF_SEGMENT_MAX_DATA_BYTES =
    RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN;
static constexpr U8 RF_INTER_SEGMENT_GAP_MS = 8;
static constexpr U8 RF_PAYLOAD_INTER_PACKET_GAP_MS = 15;
static constexpr U8 RF_GROUND_TX_ACK_REQUIRED_CCSDS = 1;
static constexpr U8 RF_GROUND_TX_ACK_REQUIRED_PAYLOAD = 0;
static constexpr U8 RF_SATELLITE_TX_ACK_REQUIRED_CCSDS = 0;
static constexpr U8 RF_SATELLITE_TX_ACK_REQUIRED_PAYLOAD = 0;
static constexpr FwSizeType PAYLOAD_PACKET_MAX_BYTES = RF_SEGMENT_MAX_DATA_BYTES;
static constexpr FwSizeType PAYLOAD_PACKET_DATA_BYTES = 35;
static constexpr U32 PAYLOAD_PACKETS_PER_RUN = 22;
static constexpr U32 PAYLOAD_RETRY_PACKETS_PER_RUN = 22;
static constexpr U8 PAYLOAD_MAGIC_0 = 0x4E;  // 'N'
static constexpr U8 PAYLOAD_MAGIC_1 = 0x32;  // '2'

inline bool isValidChannel(const U8 channel) {
    return channel < CHANNEL_COUNT;
}

inline bool rfGroundTxAckRequiredForChannel(const U8 channel) {
    return channel == CHANNEL_PAYLOAD
               ? RF_GROUND_TX_ACK_REQUIRED_PAYLOAD != 0
               : RF_GROUND_TX_ACK_REQUIRED_CCSDS != 0;
}

inline bool rfSatelliteTxAckRequiredForChannel(const U8 channel) {
    return channel == CHANNEL_PAYLOAD
               ? RF_SATELLITE_TX_ACK_REQUIRED_PAYLOAD != 0
               : RF_SATELLITE_TX_ACK_REQUIRED_CCSDS != 0;
}

}  // namespace LinkCfg
}  // namespace Components

#endif
