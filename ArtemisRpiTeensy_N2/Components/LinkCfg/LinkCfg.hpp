#ifndef Components_LinkCfg_HPP
#define Components_LinkCfg_HPP

#include <Fw/FPrimeBasicTypes.hpp>

namespace Components {
namespace LinkCfg {

static constexpr U8 CHANNEL_CCSDS = 0;
static constexpr U8 CHANNEL_PAYLOAD = 1;
static constexpr U8 CHANNEL_COUNT = 2;

static constexpr U8 UART_FRAME_MAGIC_0 = 0xD4;
static constexpr U8 UART_FRAME_MAGIC_1 = 0xC3;
static constexpr FwSizeType UART_FRAME_MAX_PAYLOAD = 220;
static constexpr FwSizeType UART_FRAME_OVERHEAD = 7;
static constexpr FwSizeType UART_FRAME_MAX_ENCODED =
    UART_FRAME_MAX_PAYLOAD + UART_FRAME_OVERHEAD;

static constexpr FwSizeType RF_SEGMENT_MAX_DATA_BYTES = 44;
static constexpr FwSizeType PAYLOAD_PACKET_MAX_BYTES = RF_SEGMENT_MAX_DATA_BYTES;
static constexpr FwSizeType PAYLOAD_PACKET_DATA_BYTES = 35;
static constexpr U8 PAYLOAD_MAGIC_0 = 0x4E;  // 'N'
static constexpr U8 PAYLOAD_MAGIC_1 = 0x32;  // '2'

inline bool isValidChannel(const U8 channel) {
    return channel < CHANNEL_COUNT;
}

}  // namespace LinkCfg
}  // namespace Components

#endif
