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

void PayloadDownlinkAppTester::testFileBackedVariableLengthPackets() {
    const U8 payload[] = {
        0x00, 0x4E, 0x32, 0xFF, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
        0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C,
        0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24
    };
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 11, sizeof(payload));
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(1);

    this->invoke_to_run(0, 0);
    ASSERT_from_packetOut_SIZE(2);
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    ASSERT_from_packetOut_SIZE(4);
    ASSERT_EQ(this->m_packets.size(), 4U);

    EXPECT_EQ(this->m_packets[0].size(), 17U);
    EXPECT_EQ(this->m_packets[1].size(), 44U);
    EXPECT_EQ(this->m_packets[2].size(), 14U);
    EXPECT_EQ(this->m_packets[3].size(), 8U);

    EXPECT_EQ(this->m_packets[0][0], LinkCfg::PAYLOAD_MAGIC_0);
    EXPECT_EQ(this->m_packets[0][1], LinkCfg::PAYLOAD_MAGIC_1);
    EXPECT_EQ(this->m_packets[0][2], 1U);
    EXPECT_EQ(this->m_packets[1][2], 2U);
    EXPECT_EQ(this->m_packets[1][6], LinkCfg::PAYLOAD_PACKET_DATA_BYTES);
    EXPECT_EQ(this->m_packets[2][2], 2U);
    EXPECT_EQ(this->m_packets[2][6], sizeof(payload) - LinkCfg::PAYLOAD_PACKET_DATA_BYTES);
    EXPECT_EQ(this->m_packets[3][2], 3U);

    for (FwSizeType i = 0; i < LinkCfg::PAYLOAD_PACKET_DATA_BYTES; ++i) {
        EXPECT_EQ(this->m_packets[1][7 + i], payload[i]);
    }
    for (FwSizeType i = 0; i < (sizeof(payload) - LinkCfg::PAYLOAD_PACKET_DATA_BYTES); ++i) {
        EXPECT_EQ(this->m_packets[2][7 + i], payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES + i]);
    }
}

void PayloadDownlinkAppTester::testProgressEventsEveryTenPercent() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES * 10U] = {};
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
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(0);

    for (U32 expectedPercent = 10; expectedPercent < 100; expectedPercent += 10) {
        this->invoke_to_run(0, 0);
        const U32 sampleIndex = (expectedPercent / 10U) - 1U;
        ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(expectedPercent / 10U);
        ASSERT_EVENTS_PayloadDownlinkProgress(
            sampleIndex,
            1,
            expectedPercent,
            expectedPercent / 10U,
            10);
    }

    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(9);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    ASSERT_TLM_ProgressPercent_SIZE(1);
    ASSERT_TLM_ProgressPercent(0, 50);
    ASSERT_TLM_ProgressPacketsSent_SIZE(1);
    ASSERT_TLM_ProgressPacketsSent(0, 5);
    ASSERT_TLM_ProgressTotalPackets_SIZE(1);
    ASSERT_TLM_ProgressTotalPackets(0, 10);
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(10);
    ASSERT_EVENTS_PayloadDownlinkProgress(9, 1, 100, 10, 10);
    ASSERT_TLM_ProgressPercent_SIZE(1);

    U8 smallPayload[(LinkCfg::PAYLOAD_PACKET_DATA_BYTES * 2U) + 1U] = {};
    for (FwSizeType i = 0; i < sizeof(smallPayload); ++i) {
        smallPayload[i] = static_cast<U8>((i + 1U) & 0xFFU);
    }
    this->writePayloadFile(smallPayload, sizeof(smallPayload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 13, sizeof(smallPayload));
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(0);

    while (this->m_packets.size() < 5U) {
        this->invoke_to_run(0, 0);
    }
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(2);
    ASSERT_EVENTS_PayloadDownlinkProgress(0, 2, 40, 1, 3);
    ASSERT_EVENTS_PayloadDownlinkProgress(1, 2, 90, 2, 3);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(3);
    ASSERT_EVENTS_PayloadDownlinkProgress(2, 2, 100, 3, 3);
    this->clearHistory();
    this->sendCmd_GET_PAYLOAD_STATUS(0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkProgress_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkProgress(0, 2, 100, 3, 3);
    ASSERT_EVENTS_PayloadStatus_SIZE(1);
    ASSERT_TLM_ProgressPercent_SIZE(1);
    ASSERT_TLM_ProgressPercent(0, 100);
    ASSERT_TLM_ProgressPacketsSent_SIZE(1);
    ASSERT_TLM_ProgressPacketsSent(0, 3);
    ASSERT_TLM_ProgressTotalPackets_SIZE(1);
    ASSERT_TLM_ProgressTotalPackets(0, 3);
}

void PayloadDownlinkAppTester::testQueuesRetryPacketsForScheduledResend() {
    const U8 payload[] = {
        0x00, 0x4E, 0x32, 0xFF, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
        0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C,
        0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24
    };
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 11, sizeof(payload));
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->invoke_to_run(0, 0);
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    ASSERT_from_packetOut_SIZE(4);

    U8 retry[] = {
        LinkCfg::PAYLOAD_MAGIC_0,
        LinkCfg::PAYLOAD_MAGIC_1,
        4,
        1,
        1,
        0,
        1,
        1
    };
    Fw::Buffer retryBuffer(retry, sizeof(retry));
    this->invoke_to_packetIn(0, retryBuffer);

    ASSERT_EVENTS_PayloadRetryRequested_SIZE(1);
    ASSERT_from_packetOut_SIZE(4);

    this->invoke_to_run(0, 0);
    ASSERT_from_packetOut_SIZE(5);
    ASSERT_EQ(this->m_packets.size(), 5U);
    EXPECT_EQ(this->m_packets[4][2], 2U);
    EXPECT_EQ(this->m_packets[4][4], 1U);
    EXPECT_EQ(this->m_packets[4][5], 0U);
}

}  // namespace Components
