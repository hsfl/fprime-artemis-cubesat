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
static constexpr U8 TEENSY_TARGET_PAYLOAD_CACHE = 3;
static constexpr U8 TEENSY_TARGET_LEPTON_PREVIEW = 4;
static constexpr U8 TEENSY_STATUS_OK = 0;
static constexpr U8 TEENSY_STATUS_BAD_REQUEST = 1;
static constexpr U8 TEENSY_STATUS_BUSY = 2;
static constexpr U8 TEENSY_STATUS_TIMEOUT = 3;
static constexpr U8 TEENSY_STATUS_TARGET_ERROR = 4;
static constexpr U8 TEENSY_RF_OP_LINK_STATS = 1;
static constexpr U8 TEENSY_RF_OP_STATUS = 1;
static constexpr U8 TEENSY_RF_OP_SET_ENABLED = 2;
static constexpr U8 TEENSY_RF_STATE_OFF = 0;
static constexpr U8 TEENSY_RF_STATE_READY = 1;
static constexpr U8 TEENSY_RF_FAULT_NONE = 0;
static constexpr U8 TEENSY_RF_FAULT_INIT_FAILED = 1;
static constexpr U8 TEENSY_RF_FAULT_WATCHDOG_RESET = 2;
static constexpr U8 TEENSY_RF_FAULT_LOCAL_TX = 3;
static constexpr U8 TEENSY_RF_BOOT_FLAG_WATCHDOG = 1;
static constexpr U32 TEENSY_RF_RSSI_AGE_UNKNOWN_MS = 4294967295;

static constexpr U8 UART_FRAME_MAGIC_0 = 0xD4;
static constexpr U8 UART_FRAME_MAGIC_1 = 0xC3;
static constexpr FwSizeType UART_FRAME_MAX_PAYLOAD = 220;
static constexpr U32 UART_BAUD = 115200;
static constexpr U32 UART_INTER_FRAME_MARGIN_US = 37000;
static constexpr U32 UART_PAYLOAD_CACHE_MARGIN_US = 1000;
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
static constexpr U16 RF_TX_COMPLETE_TIMEOUT_MS = 500;
static constexpr U8 RF_GROUND_TX_ACK_REQUIRED_CCSDS = 1;
static constexpr U8 RF_GROUND_TX_ACK_REQUIRED_PAYLOAD = 0;
static constexpr U8 RF_SATELLITE_TX_ACK_REQUIRED_CCSDS = 0;
static constexpr U8 RF_SATELLITE_TX_ACK_REQUIRED_PAYLOAD = 0;
static constexpr FwSizeType PAYLOAD_PACKET_MAX_BYTES = RF_SEGMENT_MAX_DATA_BYTES;
static constexpr FwSizeType PAYLOAD_PACKET_DATA_BYTES = 35;
static constexpr U32 PAYLOAD_CACHE_MAX_BYTES = 196608;
static constexpr FwSizeType PAYLOAD_CACHE_CHUNK_BYTES = 200;
static constexpr U8 PAYLOAD_CACHE_RF_GAP_MS = 2;
static constexpr U8 PAYLOAD_CACHE_OP_BEGIN = 1;
static constexpr U8 PAYLOAD_CACHE_OP_CHUNK = 2;
static constexpr U8 PAYLOAD_CACHE_OP_COMMIT_AND_SEND = 3;
static constexpr U8 PAYLOAD_CACHE_OP_ABORT = 4;
static constexpr U8 PAYLOAD_CACHE_STATE_EMPTY = 0;
static constexpr U8 PAYLOAD_CACHE_STATE_RECEIVING = 1;
static constexpr U8 PAYLOAD_CACHE_STATE_READY = 2;
static constexpr U8 PAYLOAD_CACHE_STATE_SENDING = 3;
static constexpr U8 PAYLOAD_CACHE_STATE_ERROR = 4;
static constexpr U32 PAYLOAD_PACKETS_PER_RUN = 18;
static constexpr U32 PAYLOAD_RETRY_PACKETS_PER_RUN = 18;
static constexpr U8 PAYLOAD_MAGIC_0 = 0x4E;  // 'N'
static constexpr U8 PAYLOAD_MAGIC_1 = 0x32;  // '2'

static constexpr U32 LEPTON_PREVIEW_MAX_FRAME_BYTES = 4800;
static constexpr FwSizeType LEPTON_PREVIEW_CHUNK_BYTES = 200;
static constexpr U8 LEPTON_PREVIEW_RF_GAP_MS = 2;
static constexpr U8 LEPTON_PREVIEW_WIDTH = 80;
static constexpr U8 LEPTON_PREVIEW_HEIGHT = 60;
static constexpr U8 LEPTON_PREVIEW_PIXEL_FORMAT_U8 = 1;
static constexpr U8 LEPTON_PREVIEW_OP_BEGIN = 1;
static constexpr U8 LEPTON_PREVIEW_OP_CHUNK = 2;
static constexpr U8 LEPTON_PREVIEW_OP_COMMIT_AND_SEND = 3;
static constexpr U8 LEPTON_PREVIEW_OP_ABORT = 4;
static constexpr U8 LEPTON_PREVIEW_STATE_EMPTY = 0;
static constexpr U8 LEPTON_PREVIEW_STATE_RECEIVING = 1;
static constexpr U8 LEPTON_PREVIEW_STATE_READY = 2;
static constexpr U8 LEPTON_PREVIEW_STATE_SENDING = 3;
static constexpr U8 LEPTON_PREVIEW_STATE_ERROR = 4;
static constexpr U8 LEPTON_PREVIEW_WIRE_MAGIC_0 = 0x50;
static constexpr U8 LEPTON_PREVIEW_WIRE_MAGIC_1 = 0x56;
static constexpr U8 LEPTON_PREVIEW_WIRE_VERSION = 1;
static constexpr U8 LEPTON_PREVIEW_WIRE_TYPE_FRAGMENT = 1;

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
