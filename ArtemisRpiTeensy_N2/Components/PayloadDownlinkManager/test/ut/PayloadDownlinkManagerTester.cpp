#include "PayloadDownlinkManagerTester.hpp"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace Components {

namespace {
constexpr const char* TEST_PAYLOAD_PATH = "/tmp/payload_downlink_manager_ut.bin";
}

PayloadDownlinkManagerTester::PayloadDownlinkManagerTester()
    : PayloadDownlinkManagerGTestBase("PayloadDownlinkManagerTester", MAX_HISTORY_SIZE),
      component("PayloadDownlinkManager"),
      m_packets() {
    this->initComponents();
    this->connectPorts();
}

PayloadDownlinkManagerTester::~PayloadDownlinkManagerTester() {
    this->component.deinit();
    (void)::unsetenv("NEUTRON_PAYLOAD_DOWNLINK_FILE");
    (void)::unlink(TEST_PAYLOAD_PATH);
}

void PayloadDownlinkManagerTester::from_packetOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const U8* data = fwBuffer.getData();
    this->m_packets.push_back(std::vector<U8>(data, data + fwBuffer.getSize()));
    this->pushFromPortEntry_packetOut(fwBuffer);
}

void PayloadDownlinkManagerTester::writePayloadFile(const U8* data, FwSizeType size) {
    std::FILE* file = std::fopen(TEST_PAYLOAD_PATH, "wb");
    ASSERT_NE(file, nullptr);
    const std::size_t written = std::fwrite(data, 1, static_cast<std::size_t>(size), file);
    ASSERT_EQ(written, static_cast<std::size_t>(size));
    ASSERT_EQ(std::fclose(file), 0);
    ASSERT_EQ(::setenv("NEUTRON_PAYLOAD_DOWNLINK_FILE", TEST_PAYLOAD_PATH, 1), 0);
}

void PayloadDownlinkManagerTester::testFileBackedVariableLengthPackets() {
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

void PayloadDownlinkManagerTester::testQueuesRetryPacketsForScheduledResend() {
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
