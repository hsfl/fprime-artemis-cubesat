#include "Components/UartChannelMux/UartChannelMux.hpp"

#include <Fw/Time/TimeInterval.hpp>
#include <Os/Task.hpp>

#include <cstring>

namespace Components {

namespace {
constexpr U32 FRAME_COUNTER_TLM_PERIOD = 64U;

bool shouldPublishFrameCounter(U32 count) {
    return count == 1U || (count % FRAME_COUNTER_TLM_PERIOD) == 0U;
}
}  // namespace

UartChannelMux::UartChannelMux(const char* const compName)
    : UartChannelMuxComponentBase(compName),
      m_state(ParseState::WAIT_MAGIC_0),
      m_rxChannel(LinkCfg::CHANNEL_CCSDS),
      m_rxLength(0),
      m_rxIndex(0),
      m_crcLo(0),
      m_framesTx(0),
      m_framesRx(0),
      m_frameDrops(0) {
    std::memset(this->m_rxPayload, 0, sizeof(this->m_rxPayload));
    std::memset(this->m_txFrame, 0, sizeof(this->m_txFrame));
}

UartChannelMux::~UartChannelMux() {}

Drv::ByteStreamStatus UartChannelMux::ccsdsSendIn_handler(FwIndexType portNum, Fw::Buffer& sendBuffer) {
    static_cast<void>(portNum);
    return this->sendWrapped(LinkCfg::CHANNEL_CCSDS, sendBuffer.getData(), sendBuffer.getSize());
}

void UartChannelMux::payloadSendIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const Drv::ByteStreamStatus status =
        this->sendWrapped(LinkCfg::CHANNEL_PAYLOAD, fwBuffer.getData(), fwBuffer.getSize());
    if (status != Drv::ByteStreamStatus::OP_OK) {
        this->m_frameDrops++;
        this->tlmWrite_FrameDrops(this->m_frameDrops);
        this->log_WARNING_LO_FrameDropped(10);
    }
}

void UartChannelMux::localSendIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const Drv::ByteStreamStatus status =
        this->sendWrapped(LinkCfg::CHANNEL_TEENSY_LOCAL, fwBuffer.getData(), fwBuffer.getSize());
    if (status != Drv::ByteStreamStatus::OP_OK) {
        this->m_frameDrops++;
        this->tlmWrite_FrameDrops(this->m_frameDrops);
        this->log_WARNING_LO_FrameDropped(11);
    }
}

void UartChannelMux::rfLocalSendIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const Drv::ByteStreamStatus status =
        this->sendWrapped(LinkCfg::CHANNEL_TEENSY_LOCAL, fwBuffer.getData(), fwBuffer.getSize());
    if (status != Drv::ByteStreamStatus::OP_OK) {
        this->m_frameDrops++;
        this->tlmWrite_FrameDrops(this->m_frameDrops);
        this->log_WARNING_LO_FrameDropped(12);
    }
}

void UartChannelMux::drvReceiveIn_handler(FwIndexType portNum,
                                          Fw::Buffer& buffer,
                                          const Drv::ByteStreamStatus& status) {
    static_cast<void>(portNum);
    if (status == Drv::ByteStreamStatus::OP_OK && buffer.isValid()) {
        const U8* data = buffer.getData();
        for (FwSizeType i = 0; i < buffer.getSize(); i++) {
            this->parseByte(data[i]);
        }
    }

    if (this->isConnected_drvReceiveReturnOut_OutputPort(0)) {
        this->drvReceiveReturnOut_out(0, buffer);
    }
}

void UartChannelMux::ccsdsRecvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    static_cast<void>(fwBuffer);
}

Drv::ByteStreamStatus UartChannelMux::sendWrapped(U8 channel, const U8* data, FwSizeType size) {
    if (!LinkCfg::isValidChannel(channel) || data == nullptr || size == 0 ||
        size > LinkCfg::UART_FRAME_MAX_PAYLOAD) {
        this->m_frameDrops++;
        this->tlmWrite_FrameDrops(this->m_frameDrops);
        this->log_WARNING_LO_FrameDropped(1);
        return Drv::ByteStreamStatus::OTHER_ERROR;
    }

    const U16 crc = this->crc16Ccitt(data, size);
    this->m_txFrame[0] = LinkCfg::UART_FRAME_MAGIC_0;
    this->m_txFrame[1] = LinkCfg::UART_FRAME_MAGIC_1;
    this->m_txFrame[2] = channel;
    this->m_txFrame[3] = static_cast<U8>(size & 0xFFU);
    this->m_txFrame[4] = static_cast<U8>((size >> 8U) & 0xFFU);
    std::memcpy(&this->m_txFrame[5], data, static_cast<size_t>(size));
    this->m_txFrame[5 + size] = static_cast<U8>(crc & 0xFFU);
    this->m_txFrame[6 + size] = static_cast<U8>((crc >> 8U) & 0xFFU);

    Fw::Buffer wrapped(this->m_txFrame, size + LinkCfg::UART_FRAME_OVERHEAD);
    const Drv::ByteStreamStatus status = this->drvSendOut_out(0, wrapped);
    if (status == Drv::ByteStreamStatus::OP_OK) {
        this->m_framesTx++;
        // Publishing this channel for every frame creates self-generated
        // CCSDS traffic: the counter update itself is another transmitted
        // frame. Sample the counter so observability cannot starve payload.
        if (shouldPublishFrameCounter(this->m_framesTx)) {
            this->tlmWrite_FramesTx(this->m_framesTx);
        }
        // Linux accepts a complete UART write before the bytes have left the
        // wire. Pace at the shared physical boundary by the actual 8N1 wire
        // time, plus a small scheduling margin, so long channel-0 frames
        // cannot backlog and overrun later channel-1 frames (or vice versa).
        if (LinkCfg::UART_BAUD > 0U) {
            const U64 delayUs = interFrameDelayUs(channel, size);
            (void)Os::Task::delay(
                Fw::TimeInterval(static_cast<U32>(delayUs / 1000000ULL),
                                 static_cast<U32>(delayUs % 1000000ULL)));
        }
    }
    return status;
}

U64 UartChannelMux::interFrameDelayUs(U8 channel, FwSizeType size) {
    if (LinkCfg::UART_BAUD == 0U) {
        return 0U;
    }
    const U64 encodedBytes = static_cast<U64>(size + LinkCfg::UART_FRAME_OVERHEAD);
    const U64 wireTimeUs =
        ((encodedBytes * 10ULL * 1000000ULL) + LinkCfg::UART_BAUD - 1ULL) / LinkCfg::UART_BAUD;
    const U64 channelDrainUs =
        (channel == LinkCfg::CHANNEL_CCSDS) ? LinkCfg::UART_CCSDS_EXTRA_MARGIN_US : 0U;
    return wireTimeUs + LinkCfg::UART_INTER_FRAME_MARGIN_US + channelDrainUs;
}

void UartChannelMux::parseByte(U8 byte) {
    switch (this->m_state) {
        case ParseState::WAIT_MAGIC_0:
            if (byte == LinkCfg::UART_FRAME_MAGIC_0) {
                this->m_state = ParseState::WAIT_MAGIC_1;
            }
            break;

        case ParseState::WAIT_MAGIC_1:
            if (byte == LinkCfg::UART_FRAME_MAGIC_1) {
                this->m_state = ParseState::WAIT_CHANNEL;
            } else {
                this->resetParser();
            }
            break;

        case ParseState::WAIT_CHANNEL:
            if (LinkCfg::isValidChannel(byte)) {
                this->m_rxChannel = byte;
                this->m_state = ParseState::WAIT_LEN_LO;
            } else {
                this->m_frameDrops++;
                this->log_WARNING_LO_FrameDropped(2);
                this->resetParser();
            }
            break;

        case ParseState::WAIT_LEN_LO:
            this->m_rxLength = byte;
            this->m_state = ParseState::WAIT_LEN_HI;
            break;

        case ParseState::WAIT_LEN_HI:
            this->m_rxLength |= static_cast<FwSizeType>(byte) << 8U;
            if (this->m_rxLength == 0 || this->m_rxLength > LinkCfg::UART_FRAME_MAX_PAYLOAD) {
                this->m_frameDrops++;
                this->log_WARNING_LO_FrameDropped(3);
                this->resetParser();
            } else {
                this->m_rxIndex = 0;
                this->m_state = ParseState::WAIT_PAYLOAD;
            }
            break;

        case ParseState::WAIT_PAYLOAD:
            this->m_rxPayload[this->m_rxIndex++] = byte;
            if (this->m_rxIndex >= this->m_rxLength) {
                this->m_state = ParseState::WAIT_CRC_LO;
            }
            break;

        case ParseState::WAIT_CRC_LO:
            this->m_crcLo = byte;
            this->m_state = ParseState::WAIT_CRC_HI;
            break;

        case ParseState::WAIT_CRC_HI: {
            const U16 frameCrc = static_cast<U16>(this->m_crcLo) | (static_cast<U16>(byte) << 8U);
            const U16 calc = this->crc16Ccitt(this->m_rxPayload, this->m_rxLength);
            if (frameCrc == calc) {
                this->handleFrame();
            } else {
                this->m_frameDrops++;
                this->tlmWrite_FrameDrops(this->m_frameDrops);
                this->log_WARNING_LO_FrameDropped(4);
            }
            this->resetParser();
            break;
        }
    }
}

void UartChannelMux::resetParser() {
    this->m_state = ParseState::WAIT_MAGIC_0;
    this->m_rxChannel = LinkCfg::CHANNEL_CCSDS;
    this->m_rxLength = 0;
    this->m_rxIndex = 0;
    this->m_crcLo = 0;
}

void UartChannelMux::handleFrame() {
    Fw::Buffer frame(this->m_rxPayload, this->m_rxLength);
    if (this->m_rxChannel == LinkCfg::CHANNEL_CCSDS) {
        this->ccsdsRecvOut_out(0, frame, Drv::ByteStreamStatus::OP_OK);
    } else if ((this->m_rxChannel == LinkCfg::CHANNEL_PAYLOAD) && this->isConnected_payloadRecvOut_OutputPort(0)) {
        this->payloadRecvOut_out(0, frame);
    } else if (this->m_rxChannel == LinkCfg::CHANNEL_TEENSY_LOCAL) {
        const U8 target = (this->m_rxLength > 0) ? this->m_rxPayload[0] : 0;
        if ((target == LinkCfg::TEENSY_TARGET_RF_STATUS) && this->isConnected_rfLocalRecvOut_OutputPort(0)) {
            this->rfLocalRecvOut_out(0, frame);
        } else if (this->isConnected_localRecvOut_OutputPort(0)) {
            this->localRecvOut_out(0, frame);
        }
    }

    this->m_framesRx++;
    if (shouldPublishFrameCounter(this->m_framesRx)) {
        this->tlmWrite_FramesRx(this->m_framesRx);
    }
}

U16 UartChannelMux::crc16Ccitt(const U8* data, FwSizeType size) const {
    U16 crc = 0xFFFFU;
    for (FwSizeType i = 0; i < size; i++) {
        crc ^= static_cast<U16>(data[i]) << 8U;
        for (U8 bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0) {
                crc = static_cast<U16>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<U16>(crc << 1U);
            }
        }
    }
    return crc;
}

}  // namespace Components
