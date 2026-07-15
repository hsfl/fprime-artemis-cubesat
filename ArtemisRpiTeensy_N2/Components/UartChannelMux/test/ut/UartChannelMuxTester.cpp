#include "UartChannelMuxTester.hpp"

namespace Components {

UartChannelMuxTester::UartChannelMuxTester()
    : UartChannelMuxGTestBase("UartChannelMuxTester", MAX_HISTORY_SIZE),
      component("UartChannelMux"),
      m_drvSendStatus(Drv::ByteStreamStatus::OP_OK),
      m_txFrames(),
      m_ccsdsFrames(),
      m_payloadFrames(),
      m_localFrames() {
    this->initComponents();
    this->connectPorts();
}

UartChannelMuxTester::~UartChannelMuxTester() {
    this->component.deinit();
}

Drv::ByteStreamStatus UartChannelMuxTester::from_drvSendOut_handler(
    FwIndexType portNum,
    Fw::Buffer& fwBuffer
) {
    static_cast<void>(portNum);
    const U8* data = fwBuffer.getData();
    this->m_txFrames.push_back(std::vector<U8>(data, data + fwBuffer.getSize()));
    this->pushFromPortEntry_drvSendOut(fwBuffer);
    return this->m_drvSendStatus;
}

void UartChannelMuxTester::from_ccsdsRecvOut_handler(
    FwIndexType portNum,
    Fw::Buffer& fwBuffer,
    const Drv::ByteStreamStatus& status
) {
    static_cast<void>(portNum);
    const U8* data = fwBuffer.getData();
    this->m_ccsdsFrames.push_back(std::vector<U8>(data, data + fwBuffer.getSize()));
    this->pushFromPortEntry_ccsdsRecvOut(fwBuffer, status);
}

void UartChannelMuxTester::from_payloadRecvOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const U8* data = fwBuffer.getData();
    this->m_payloadFrames.push_back(std::vector<U8>(data, data + fwBuffer.getSize()));
    this->pushFromPortEntry_payloadRecvOut(fwBuffer);
}

void UartChannelMuxTester::from_localRecvOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const U8* data = fwBuffer.getData();
    this->m_localFrames.push_back(std::vector<U8>(data, data + fwBuffer.getSize()));
    this->pushFromPortEntry_localRecvOut(fwBuffer);
}

void UartChannelMuxTester::testWrapsAndRoutesChannelFrames() {
    const U8 payloadBytes[] = {0x4E, 0x32, 0x01, 0x02, 0x03};
    const U8 localBytes[] = {LinkCfg::TEENSY_TARGET_PDU, 0x22, 0x03, 0x00};

    this->clearHistory();
    this->m_txFrames.clear();
    this->m_payloadFrames.clear();
    this->m_localFrames.clear();

    const U64 encodedBytes = sizeof(payloadBytes) + LinkCfg::UART_FRAME_OVERHEAD;
    const U64 wireTimeUs =
        ((encodedBytes * 10ULL * 1000000ULL) + LinkCfg::UART_BAUD - 1ULL) / LinkCfg::UART_BAUD;
    EXPECT_EQ(UartChannelMux::interFrameDelayUs(LinkCfg::CHANNEL_PAYLOAD, sizeof(payloadBytes)),
              wireTimeUs + LinkCfg::UART_INTER_FRAME_MARGIN_US);
    EXPECT_EQ(UartChannelMux::interFrameDelayUs(LinkCfg::CHANNEL_CCSDS, sizeof(payloadBytes)),
              wireTimeUs + LinkCfg::UART_INTER_FRAME_MARGIN_US + LinkCfg::UART_CCSDS_EXTRA_MARGIN_US);

    Fw::Buffer payloadBuffer(const_cast<U8*>(payloadBytes), sizeof(payloadBytes));
    this->invoke_to_payloadSendIn(0, payloadBuffer);
    ASSERT_from_drvSendOut_SIZE(1);
    ASSERT_EQ(this->m_txFrames.size(), 1U);
    ASSERT_EQ(this->m_txFrames[0].size(), sizeof(payloadBytes) + LinkCfg::UART_FRAME_OVERHEAD);
    EXPECT_EQ(this->m_txFrames[0][0], LinkCfg::UART_FRAME_MAGIC_0);
    EXPECT_EQ(this->m_txFrames[0][1], LinkCfg::UART_FRAME_MAGIC_1);
    EXPECT_EQ(this->m_txFrames[0][2], LinkCfg::CHANNEL_PAYLOAD);
    EXPECT_EQ(this->m_txFrames[0][3], sizeof(payloadBytes));
    EXPECT_EQ(this->m_txFrames[0][4], 0U);
    for (FwSizeType i = 0; i < sizeof(payloadBytes); ++i) {
        EXPECT_EQ(this->m_txFrames[0][5 + i], payloadBytes[i]);
    }

    Fw::Buffer localBuffer(const_cast<U8*>(localBytes), sizeof(localBytes));
    this->invoke_to_localSendIn(0, localBuffer);
    ASSERT_from_drvSendOut_SIZE(2);
    ASSERT_EQ(this->m_txFrames.size(), 2U);
    EXPECT_EQ(this->m_txFrames[1][2], LinkCfg::CHANNEL_TEENSY_LOCAL);
    ASSERT_TLM_FramesTx_SIZE(1);
    ASSERT_TLM_FramesTx(0, 1);

    // Frame-counter telemetry is sampled to avoid creating one CCSDS packet
    // for every payload packet (and recursively counting that packet too).
    for (U32 count = 2U; count < 64U; ++count) {
        this->invoke_to_payloadSendIn(0, payloadBuffer);
    }
    ASSERT_from_drvSendOut_SIZE(64);
    ASSERT_TLM_FramesTx_SIZE(2);
    ASSERT_TLM_FramesTx(1, 64);

    const std::vector<U8> localFrame = makeFrame(LinkCfg::CHANNEL_TEENSY_LOCAL, localBytes, sizeof(localBytes));
    const FwSizeType split = 3;
    Fw::Buffer localFramePart0(const_cast<U8*>(localFrame.data()), split);
    Fw::Buffer localFramePart1(
        const_cast<U8*>(localFrame.data() + split),
        static_cast<FwSizeType>(localFrame.size() - split));
    this->invoke_to_drvReceiveIn(0, localFramePart0, Drv::ByteStreamStatus::OP_OK);
    ASSERT_from_localRecvOut_SIZE(0);
    this->invoke_to_drvReceiveIn(0, localFramePart1, Drv::ByteStreamStatus::OP_OK);
    ASSERT_from_localRecvOut_SIZE(1);
    ASSERT_EQ(this->m_localFrames.size(), 1U);
    ASSERT_EQ(this->m_localFrames[0].size(), sizeof(localBytes));
    for (FwSizeType i = 0; i < sizeof(localBytes); ++i) {
        EXPECT_EQ(this->m_localFrames[0][i], localBytes[i]);
    }

    const std::vector<U8> payloadFrame =
        makeFrame(LinkCfg::CHANNEL_PAYLOAD, payloadBytes, sizeof(payloadBytes));
    Fw::Buffer incomingPayload(const_cast<U8*>(payloadFrame.data()), payloadFrame.size());
    this->invoke_to_drvReceiveIn(0, incomingPayload, Drv::ByteStreamStatus::OP_OK);
    ASSERT_from_payloadRecvOut_SIZE(1);
    ASSERT_EQ(this->m_payloadFrames.size(), 1U);
    ASSERT_EQ(this->m_payloadFrames[0].size(), sizeof(payloadBytes));
    for (FwSizeType i = 0; i < sizeof(payloadBytes); ++i) {
        EXPECT_EQ(this->m_payloadFrames[0][i], payloadBytes[i]);
    }
}

void UartChannelMuxTester::testPropagatesPayloadLocalAcceptanceStatus() {
    U8 payloadBytes[] = {0x4E, 0x32, 0x02, 0x01};
    const U8 originalFirstByte = payloadBytes[0];
    Fw::Buffer payloadBuffer(payloadBytes, sizeof(payloadBytes));

    this->m_drvSendStatus = Drv::ByteStreamStatus::OP_OK;
    EXPECT_EQ(this->invoke_to_payloadSendIn(0, payloadBuffer),
              Components::PayloadSendStatus::LOCAL_ACCEPTED);
    payloadBytes[0] = 0U;
    ASSERT_EQ(this->m_txFrames.size(), 1U);
    EXPECT_EQ(this->m_txFrames[0][5], originalFirstByte);

    this->m_drvSendStatus = Drv::ByteStreamStatus::SEND_RETRY;
    EXPECT_EQ(this->invoke_to_payloadSendIn(0, payloadBuffer),
              Components::PayloadSendStatus::LOCAL_RETRY);

    this->m_drvSendStatus = Drv::ByteStreamStatus::OTHER_ERROR;
    EXPECT_EQ(this->invoke_to_payloadSendIn(0, payloadBuffer),
              Components::PayloadSendStatus::LOCAL_ERROR);
    ASSERT_EVENTS_FrameDropped_SIZE(2);
    ASSERT_TLM_FrameDrops(1, 2);
    ASSERT_TLM_FramesTx_SIZE(1);
    ASSERT_TLM_FramesTx(0, 1);
}

std::vector<U8> UartChannelMuxTester::makeFrame(U8 channel, const U8* payload, FwSizeType size) {
    std::vector<U8> frame;
    frame.reserve(static_cast<std::size_t>(size + LinkCfg::UART_FRAME_OVERHEAD));
    frame.push_back(LinkCfg::UART_FRAME_MAGIC_0);
    frame.push_back(LinkCfg::UART_FRAME_MAGIC_1);
    frame.push_back(channel);
    frame.push_back(static_cast<U8>(size & 0xFFU));
    frame.push_back(static_cast<U8>((size >> 8U) & 0xFFU));
    for (FwSizeType i = 0; i < size; ++i) {
        frame.push_back(payload[i]);
    }
    const U16 crc = crc16Ccitt(payload, size);
    frame.push_back(static_cast<U8>(crc & 0xFFU));
    frame.push_back(static_cast<U8>((crc >> 8U) & 0xFFU));
    return frame;
}

U16 UartChannelMuxTester::crc16Ccitt(const U8* data, FwSizeType size) {
    U16 crc = 0xFFFFU;
    for (FwSizeType i = 0; i < size; ++i) {
        crc ^= static_cast<U16>(data[i]) << 8U;
        for (U8 bit = 0; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<U16>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<U16>(crc << 1U);
            }
        }
    }
    return crc;
}

}  // namespace Components
