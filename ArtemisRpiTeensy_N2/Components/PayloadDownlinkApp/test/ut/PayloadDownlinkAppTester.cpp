#include "PayloadDownlinkAppTester.hpp"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace Components {

namespace {
constexpr const char* TEST_PAYLOAD_PATH = "/tmp/payload_downlink_manager_ut.bin";
}

PayloadDownlinkAppTester::PayloadDownlinkAppTester()
    : PayloadDownlinkAppGTestBase("PayloadDownlinkAppTester", MAX_HISTORY_SIZE),
      component("PayloadDownlinkApp"),
      m_packets() {
    this->initComponents();
    this->connectPorts();
}

PayloadDownlinkAppTester::~PayloadDownlinkAppTester() {
    this->component.deinit();
    (void)::unsetenv("NEUTRON_PAYLOAD_DOWNLINK_FILE");
    (void)::unlink(TEST_PAYLOAD_PATH);
}

void PayloadDownlinkAppTester::from_packetOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const U8* data = fwBuffer.getData();
    this->m_packets.push_back(std::vector<U8>(data, data + fwBuffer.getSize()));
    this->pushFromPortEntry_packetOut(fwBuffer);
}

void PayloadDownlinkAppTester::writePayloadFile(const U8* data, FwSizeType size) {
    std::FILE* file = std::fopen(TEST_PAYLOAD_PATH, "wb");
    ASSERT_NE(file, nullptr);
    const std::size_t written = std::fwrite(data, 1, static_cast<std::size_t>(size), file);
    ASSERT_EQ(written, static_cast<std::size_t>(size));
    ASSERT_EQ(std::fclose(file), 0);
    ASSERT_EQ(::setenv("NEUTRON_PAYLOAD_DOWNLINK_FILE", TEST_PAYLOAD_PATH, 1), 0);
}

void PayloadDownlinkAppTester::testHeaderRetransmitBehavior() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES] = {};
    for (FwSizeType i = 0; i < sizeof(payload); ++i) {
        payload[i] = static_cast<U8>(i & 0xFFU);
    }
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 11, sizeof(payload));
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(1);

    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkProgress(0, 1, 90, 1, 1);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    ASSERT_from_packetOut_SIZE(5);
    ASSERT_EQ(this->m_packets.size(), 5U);

    EXPECT_EQ(this->m_packets[0].size(), 17U);
    EXPECT_EQ(this->m_packets[1].size(), 17U);
    EXPECT_EQ(this->m_packets[2].size(), 17U);
    EXPECT_EQ(this->m_packets[3].size(), 44U);
    EXPECT_EQ(this->m_packets[4].size(), 8U);

    EXPECT_EQ(this->m_packets[0][2], 1U);
    EXPECT_EQ(this->m_packets[1][2], 1U);
    EXPECT_EQ(this->m_packets[2][2], 1U);
    EXPECT_EQ(this->m_packets[3][2], 2U);
    EXPECT_EQ(this->m_packets[4][2], 3U);
    for (FwSizeType i = 0; i < LinkCfg::PAYLOAD_PACKET_DATA_BYTES; ++i) {
        EXPECT_EQ(this->m_packets[3][7 + i], payload[i]);
    }
}

void PayloadDownlinkAppTester::testBurstCountSendsGeneratedPayloadPacketsPerRun() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES * (LinkCfg::PAYLOAD_PACKETS_PER_RUN + 1U)] = {};
    for (FwSizeType i = 0; i < sizeof(payload); ++i) {
        payload[i] = static_cast<U8>(i & 0xFFU);
    }
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 12, sizeof(payload));
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(1);

    this->invoke_to_run(0, 0);
    const U32 headerPackets = 3U;
    const U32 firstRunDataPackets = LinkCfg::PAYLOAD_PACKETS_PER_RUN - headerPackets;
    ASSERT_from_packetOut_SIZE(LinkCfg::PAYLOAD_PACKETS_PER_RUN);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(0);
    ASSERT_EQ(this->m_packets.size(), LinkCfg::PAYLOAD_PACKETS_PER_RUN);
    EXPECT_EQ(this->m_packets[0][2], 1U);
    EXPECT_EQ(this->m_packets[1][2], 1U);
    EXPECT_EQ(this->m_packets[2][2], 1U);
    EXPECT_EQ(this->m_packets[3][2], 2U);
    EXPECT_EQ(this->m_packets[3][4], 0U);
    EXPECT_EQ(this->m_packets[LinkCfg::PAYLOAD_PACKETS_PER_RUN - 1U][2], 2U);
    EXPECT_EQ(this->m_packets[LinkCfg::PAYLOAD_PACKETS_PER_RUN - 1U][4], firstRunDataPackets - 1U);

    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    ASSERT_from_packetOut_SIZE(headerPackets + LinkCfg::PAYLOAD_PACKETS_PER_RUN + 2U);
    ASSERT_EQ(this->m_packets.size(), headerPackets + LinkCfg::PAYLOAD_PACKETS_PER_RUN + 2U);
    EXPECT_EQ(this->m_packets[LinkCfg::PAYLOAD_PACKETS_PER_RUN][2], 2U);
    EXPECT_EQ(this->m_packets[LinkCfg::PAYLOAD_PACKETS_PER_RUN][4], firstRunDataPackets);
    EXPECT_EQ(this->m_packets[headerPackets + LinkCfg::PAYLOAD_PACKETS_PER_RUN + 1U][2], 3U);
}

void PayloadDownlinkAppTester::testProgressEventsEveryTenPercent() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES * 10U] = {};
    for (FwSizeType i = 0; i < sizeof(payload); ++i) {
        payload[i] = static_cast<U8>(i & 0xFFU);
    }
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 13, sizeof(payload));
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(1);

    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(9);
    for (U32 expectedPercent = 10; expectedPercent < 100; expectedPercent += 10) {
        const U32 sampleIndex = (expectedPercent / 10U) - 1U;
        ASSERT_EVENTS_PayloadDownlinkProgress(
            sampleIndex,
            1,
            expectedPercent,
            expectedPercent / 10U,
            10);
    }
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);

    this->clearHistory();
    this->sendCmd_GET_PAYLOAD_STATUS(0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkProgress(0, 1, 100, 10, 10);
    ASSERT_EVENTS_PayloadStatus_SIZE(1);
    ASSERT_TLM_ProgressPercent_SIZE(1);
    ASSERT_TLM_ProgressPercent(0, 100);
    ASSERT_TLM_ProgressPacketsSent_SIZE(1);
    ASSERT_TLM_ProgressPacketsSent(0, 10);
    ASSERT_TLM_ProgressTotalPackets_SIZE(1);
    ASSERT_TLM_ProgressTotalPackets(0, 10);
}

void PayloadDownlinkAppTester::testRetryBurstCountSendsGeneratedRetryPacketsPerRun() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES * (LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN + 1U)] = {};
    for (FwSizeType i = 0; i < sizeof(payload); ++i) {
        payload[i] = static_cast<U8>((i + 1U) & 0xFFU);
    }
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 14, sizeof(payload));
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    const U32 headerPackets = 3U;
    const U32 initialPacketCount = headerPackets + LinkCfg::PAYLOAD_PACKETS_PER_RUN + 2U;
    ASSERT_from_packetOut_SIZE(initialPacketCount);
    ASSERT_EQ(this->m_packets.size(), initialPacketCount);

    U8 retry[] = {
        LinkCfg::PAYLOAD_MAGIC_0,
        LinkCfg::PAYLOAD_MAGIC_1,
        4,
        1,
        0,
        0,
        5,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0x01
    };
    Fw::Buffer retryBuffer(retry, sizeof(retry));
    this->invoke_to_packetIn(0, retryBuffer);

    ASSERT_EVENTS_PayloadRetryRequested_SIZE(1);

    this->invoke_to_run(0, 0);
    ASSERT_from_packetOut_SIZE(initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN);
    ASSERT_EQ(this->m_packets.size(), initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN);
    EXPECT_EQ(this->m_packets[initialPacketCount][2], 2U);
    EXPECT_EQ(this->m_packets[initialPacketCount][4], 0U);
    EXPECT_EQ(this->m_packets[initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN - 1U][2], 2U);
    EXPECT_EQ(this->m_packets[initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN - 1U][4],
              LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN - 1U);

    this->invoke_to_run(0, 0);
    ASSERT_from_packetOut_SIZE(initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN + 1U);
    ASSERT_EQ(this->m_packets.size(), initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN + 1U);
    EXPECT_EQ(this->m_packets[initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN][2], 2U);
    EXPECT_EQ(this->m_packets[initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN][4],
              LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN);
}

}  // namespace Components
