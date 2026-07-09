#ifndef Components_UartChannelMux_HPP
#define Components_UartChannelMux_HPP

#include "Components/LinkCfg/LinkCfg.hpp"
#include "Components/UartChannelMux/UartChannelMuxComponentAc.hpp"

namespace Components {

class UartChannelMux final : public UartChannelMuxComponentBase {
  public:
    UartChannelMux(const char* const compName);
    ~UartChannelMux();

  private:
    friend class UartChannelMuxTester;

    enum class ParseState {
        WAIT_MAGIC_0,
        WAIT_MAGIC_1,
        WAIT_CHANNEL,
        WAIT_LEN_LO,
        WAIT_LEN_HI,
        WAIT_PAYLOAD,
        WAIT_CRC_LO,
        WAIT_CRC_HI
    };

    Drv::ByteStreamStatus ccsdsSendIn_handler(FwIndexType portNum, Fw::Buffer& sendBuffer) override;
    void payloadSendIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void localSendIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void rfLocalSendIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void drvReceiveIn_handler(FwIndexType portNum,
                              Fw::Buffer& buffer,
                              const Drv::ByteStreamStatus& status) override;
    void ccsdsRecvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    Drv::ByteStreamStatus sendWrapped(U8 channel, const U8* data, FwSizeType size);
    static U64 interFrameDelayUs(U8 channel, FwSizeType size);
    void parseByte(U8 byte);
    void resetParser();
    void handleFrame();
    U16 crc16Ccitt(const U8* data, FwSizeType size) const;

    ParseState m_state;
    U8 m_rxChannel;
    U8 m_rxPayload[LinkCfg::UART_FRAME_MAX_PAYLOAD];
    FwSizeType m_rxLength;
    FwSizeType m_rxIndex;
    U8 m_crcLo;
    U32 m_framesTx;
    U32 m_framesRx;
    U32 m_frameDrops;
    U8 m_txFrame[LinkCfg::UART_FRAME_MAX_ENCODED];
};

}  // namespace Components

#endif
