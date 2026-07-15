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
      m_rejectedPacketType(0U),
      m_rejectedStatus(Components::PayloadSendStatus::LOCAL_ACCEPTED),
      m_rejectionsRemaining(0U),
      m_packets() {
    this->initComponents();
    this->connectPorts();
}

PayloadDownlinkAppTester::~PayloadDownlinkAppTester() {
    this->component.deinit();
    (void)::unsetenv("NEUTRON_PAYLOAD_DOWNLINK_FILE");
    (void)::unlink(TEST_PAYLOAD_PATH);
}

Components::PayloadSendStatus PayloadDownlinkAppTester::from_packetOut_handler(FwIndexType portNum,
                                                                               Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const U8* data = fwBuffer.getData();
    this->m_packets.push_back(std::vector<U8>(data, data + fwBuffer.getSize()));
    this->pushFromPortEntry_packetOut(fwBuffer);
    if ((fwBuffer.getSize() > 2U) && (data[2] == this->m_rejectedPacketType) &&
        (this->m_rejectionsRemaining > 0U)) {
        this->m_rejectionsRemaining--;
        return this->m_rejectedStatus;
    }
    return Components::PayloadSendStatus::LOCAL_ACCEPTED;
}

void PayloadDownlinkAppTester::rejectNextPacket(U8 packetType,
                                                const Components::PayloadSendStatus& status,
                                                U32 count) {
    this->m_rejectedPacketType = packetType;
    this->m_rejectedStatus = status;
    this->m_rejectionsRemaining = count;
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
    this->component.doDispatch();
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
    this->component.doDispatch();
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
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    ASSERT_from_packetOut_SIZE(headerPackets + LinkCfg::PAYLOAD_PACKETS_PER_RUN + 3U);
    ASSERT_EQ(this->m_packets.size(), headerPackets + LinkCfg::PAYLOAD_PACKETS_PER_RUN + 3U);
    EXPECT_EQ(this->m_packets[LinkCfg::PAYLOAD_PACKETS_PER_RUN][2], 1U);
    EXPECT_EQ(this->m_packets[LinkCfg::PAYLOAD_PACKETS_PER_RUN + 1U][2], 2U);
    EXPECT_EQ(this->m_packets[LinkCfg::PAYLOAD_PACKETS_PER_RUN + 1U][4], firstRunDataPackets);
    EXPECT_EQ(this->m_packets[headerPackets + LinkCfg::PAYLOAD_PACKETS_PER_RUN + 2U][2], 3U);
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
    this->component.doDispatch();
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
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    const U32 headerPackets = 3U;
    const U32 initialPacketCount = headerPackets + LinkCfg::PAYLOAD_PACKETS_PER_RUN + 3U;
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
    ASSERT_EVENTS_PayloadRetryRequested_SIZE(0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadRetryRequested_SIZE(1);
    ASSERT_from_packetOut_SIZE(initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN);
    ASSERT_EQ(this->m_packets.size(), initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN);
    EXPECT_EQ(this->m_packets[initialPacketCount][2], 2U);
    EXPECT_EQ(this->m_packets[initialPacketCount][4], 0U);
    EXPECT_EQ(this->m_packets[initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN - 1U][2], 2U);
    EXPECT_EQ(this->m_packets[initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN - 1U][4],
              LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN - 1U);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_packetOut_SIZE(initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN + 1U);
    ASSERT_EQ(this->m_packets.size(), initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN + 1U);
    EXPECT_EQ(this->m_packets[initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN][2], 2U);
    EXPECT_EQ(this->m_packets[initialPacketCount + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN][4],
              LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN);
}

void PayloadDownlinkAppTester::testActiveRequestGuardPreservesTransferAndProgress() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES * 30U] = {};
    for (FwSizeType i = 0; i < sizeof(payload); ++i) {
        payload[i] = static_cast<U8>((i + 3U) & 0xFFU);
    }
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    Fw::String sourcePath(TEST_PAYLOAD_PATH);
    this->invoke_to_downlinkRequestIn(
        0, 21, sizeof(payload), Components::ScienceProductSource::TEST, sourcePath, 0);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(0);

    this->clearHistory();
    this->invoke_to_downlinkRequestIn(
        0, 21, sizeof(payload), Components::ScienceProductSource::TEST, sourcePath, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(0);
    ASSERT_EVENTS_PayloadDownlinkRequestDuplicate_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkRequestDuplicate(0, 1, 21, 15);
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_from_statusOut(0, 1, 1, 21, sizeof(payload), 15, 30, 0);
    ASSERT_TLM_RequestDisposition(0, 1);

    this->clearHistory();
    this->invoke_to_downlinkRequestIn(
        0, 21, sizeof(payload), Components::ScienceProductSource::TEST, sourcePath, 1);
    this->component.doDispatch();

    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(0);
    ASSERT_EVENTS_PayloadDownlinkRequestConflict_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkRequestConflict(0, 1, 21, 21);
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_from_statusOut(0, 1, 1, 21, sizeof(payload), 15, 30, 0);
    ASSERT_TLM_RequestDisposition(0, 2);

    this->clearHistory();
    this->sendCmd_ABORT_PAYLOAD_DOWNLINK(0, 0);
    this->component.doDispatch();
    this->invoke_to_downlinkRequestIn(
        0, 22, sizeof(payload), Components::ScienceProductSource::TEST, sourcePath, 0);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, PayloadDownlinkAppComponentBase::OPCODE_ABORT_PAYLOAD_DOWNLINK, 0,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkStarted(0, 22, sizeof(payload), 30);
    ASSERT_from_statusOut_SIZE(2);
    ASSERT_from_statusOut(0, 3, 1, 21, sizeof(payload), 15, 30, 0);
    ASSERT_from_statusOut(1, 1, 2, 22, sizeof(payload), 0, 30, 0);
    ASSERT_TLM_TransferId(0, 2);
    ASSERT_TLM_ProductId(0, 22);
    ASSERT_TLM_ProgressPacketsSent(1, 0);
    ASSERT_TLM_RequestDisposition(0, 0);
}

void PayloadDownlinkAppTester::testControlMailboxCopiesInputAndReportsOverflow() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES] = {};
    for (FwSizeType i = 0; i < sizeof(payload); ++i) {
        payload[i] = static_cast<U8>((i + 5U) & 0xFFU);
    }
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 31, sizeof(payload));
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);

    U8 retry[] = {
        LinkCfg::PAYLOAD_MAGIC_0,
        LinkCfg::PAYLOAD_MAGIC_1,
        4,
        1,
        0,
        0,
        1,
        0x01
    };
    Fw::Buffer retryBuffer(retry, sizeof(retry));
    this->clearHistory();
    const std::size_t packetsBeforeRetry = this->m_packets.size();
    this->invoke_to_packetIn(0, retryBuffer);
    retry[3] = 99U;
    retry[7] = 0U;

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadRetryRequested_SIZE(1);
    ASSERT_EVENTS_PayloadRetryRequested(0, 0, 1);
    ASSERT_EQ(this->m_packets.size(), packetsBeforeRetry + 1U);
    EXPECT_EQ(this->m_packets.back()[2], 2U);
    EXPECT_EQ(this->m_packets.back()[4], 0U);
    ASSERT_TLM_ControlMailboxHighWater(0, 1);

    retry[3] = 1U;
    retry[7] = 0x01U;
    this->clearHistory();
    for (U32 i = 0U; i < 9U; ++i) {
        this->invoke_to_packetIn(0, retryBuffer);
    }
    this->invoke_to_run(0, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_PayloadControlPacketRejected_SIZE(1);
    ASSERT_EVENTS_PayloadControlPacketRejected(0, 2, 1);
    ASSERT_TLM_ControlMailboxDrops(0, 1);
    ASSERT_TLM_ControlMailboxHighWater(0, 8);

    U8 oversized[LinkCfg::PAYLOAD_PACKET_MAX_BYTES + 1U] = {};
    Fw::Buffer oversizedBuffer(oversized, sizeof(oversized));
    this->clearHistory();
    this->invoke_to_packetIn(0, oversizedBuffer);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadControlPacketRejected_SIZE(1);
    ASSERT_EVENTS_PayloadControlPacketRejected(0, 1, 1);
    ASSERT_TLM_ControlPacketsInvalid(0, 1);
    ASSERT_EVENTS_PayloadRetryRequested_SIZE(0);
}

void PayloadDownlinkAppTester::testRetryRequestsMergeAdditivelyAndIgnoreEmptyRequest() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES * 24U] = {};
    for (FwSizeType i = 0; i < sizeof(payload); ++i) {
        payload[i] = static_cast<U8>((i + 7U) & 0xFFU);
    }
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 41, sizeof(payload));
    this->component.doDispatch();
    for (U32 run = 0U; run < 3U; ++run) {
        this->invoke_to_run(0, 0);
        this->component.doDispatch();
    }
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    const std::size_t packetsBeforeRepair = this->m_packets.size();

    U8 firstRequest[] = {
        LinkCfg::PAYLOAD_MAGIC_0, LinkCfg::PAYLOAD_MAGIC_1, 4, 1, 0, 0, 2, 0xFF, 0xFF
    };
    U8 overlappingRequest[] = {
        LinkCfg::PAYLOAD_MAGIC_0, LinkCfg::PAYLOAD_MAGIC_1, 4, 1, 8, 0, 2, 0xFF, 0xFF
    };
    U8 emptyRequest[] = {
        LinkCfg::PAYLOAD_MAGIC_0, LinkCfg::PAYLOAD_MAGIC_1, 4, 1, 0, 0, 2, 0x00, 0x00
    };
    Fw::Buffer firstBuffer(firstRequest, sizeof(firstRequest));
    Fw::Buffer overlappingBuffer(overlappingRequest, sizeof(overlappingRequest));
    Fw::Buffer emptyBuffer(emptyRequest, sizeof(emptyRequest));

    this->clearHistory();
    this->invoke_to_packetIn(0, firstBuffer);
    this->invoke_to_packetIn(0, overlappingBuffer);
    this->invoke_to_packetIn(0, emptyBuffer);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_PayloadRetryRequested_SIZE(3);
    ASSERT_EVENTS_PayloadRetryRequested(0, 0, 16);
    ASSERT_EVENTS_PayloadRetryRequested(1, 8, 16);
    ASSERT_EVENTS_PayloadRetryRequested(2, 0, 0);
    ASSERT_EQ(this->m_packets.size(), packetsBeforeRepair + LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN);
    for (U32 packetIndex = 0U; packetIndex < LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN; ++packetIndex) {
        const std::vector<U8>& packet = this->m_packets[packetsBeforeRepair + packetIndex];
        ASSERT_EQ(packet[2], 2U);
        const U32 encodedIndex = static_cast<U32>(packet[4]) | (static_cast<U32>(packet[5]) << 8U);
        EXPECT_EQ(encodedIndex, packetIndex);
    }

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), packetsBeforeRepair + 24U);
    for (U32 packetIndex = LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN; packetIndex < 24U; ++packetIndex) {
        const std::vector<U8>& packet = this->m_packets[packetsBeforeRepair + packetIndex];
        const U32 encodedIndex = static_cast<U32>(packet[4]) | (static_cast<U32>(packet[5]) << 8U);
        EXPECT_EQ(encodedIndex, packetIndex);
    }

    const std::size_t packetsAfterRepair = this->m_packets.size();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    EXPECT_EQ(this->m_packets.size(), packetsAfterRepair);
}

void PayloadDownlinkAppTester::testAbortClearsPendingRepairAndRejectsLaterControl() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES] = {};
    this->writePayloadFile(payload, sizeof(payload));
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 42, sizeof(payload));
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);

    U8 retry[] = {
        LinkCfg::PAYLOAD_MAGIC_0, LinkCfg::PAYLOAD_MAGIC_1, 4, 1, 0, 0, 1, 0x01
    };
    Fw::Buffer retryBuffer(retry, sizeof(retry));
    this->invoke_to_packetIn(0, retryBuffer);

    this->clearHistory();
    this->sendCmd_ABORT_PAYLOAD_DOWNLINK(0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_from_statusOut(0, 3, 1, 42, sizeof(payload), 1, 1, 0);
    const std::size_t packetsAtAbort = this->m_packets.size();

    this->clearHistory();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadRetryRequested_SIZE(0);
    EXPECT_EQ(this->m_packets.size(), packetsAtAbort);

    this->clearHistory();
    this->invoke_to_packetIn(0, retryBuffer);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadRetryRequested_SIZE(0);
    ASSERT_EVENTS_PayloadControlPacketRejected_SIZE(1);
    ASSERT_EVENTS_PayloadControlPacketRejected(0, 3, 1);
    ASSERT_TLM_ControlPacketsInvalid(0, 1);
    EXPECT_EQ(this->m_packets.size(), packetsAtAbort);
}

void PayloadDownlinkAppTester::testLocalRetryPreservesNominalAndEndProgress() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES] = {};
    for (FwSizeType i = 0U; i < sizeof(payload); ++i) {
        payload[i] = static_cast<U8>((i + 17U) & 0xFFU);
    }
    this->writePayloadFile(payload, sizeof(payload));

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 51, sizeof(payload));
    this->component.doDispatch();
    this->clearHistory();
    this->m_packets.clear();
    this->rejectNextPacket(1U, Components::PayloadSendStatus::LOCAL_RETRY);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 1U);
    EXPECT_EQ(this->m_packets[0][2], 1U);
    ASSERT_EVENTS_PayloadDownlinkFailed_SIZE(0);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    ASSERT_GE(this->m_packets.size(), 2U);
    EXPECT_EQ(this->m_packets[0], this->m_packets[1]);

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 1, 52, sizeof(payload));
    this->component.doDispatch();
    this->clearHistory();
    this->m_packets.clear();
    this->rejectNextPacket(2U, Components::PayloadSendStatus::LOCAL_RETRY);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 4U);
    EXPECT_EQ(this->m_packets[3][2], 2U);
    EXPECT_EQ(this->m_packets[3][4], 0U);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    ASSERT_EQ(this->m_packets.size(), 7U);
    EXPECT_EQ(this->m_packets[4][2], 1U);
    EXPECT_EQ(this->m_packets[5], this->m_packets[3]);
    EXPECT_EQ(this->m_packets[6][2], 3U);

    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 2, 53, sizeof(payload));
    this->component.doDispatch();
    this->clearHistory();
    this->m_packets.clear();
    this->rejectNextPacket(3U, Components::PayloadSendStatus::LOCAL_RETRY);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 5U);
    EXPECT_EQ(this->m_packets[4][2], 3U);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);
    ASSERT_EQ(this->m_packets.size(), 7U);
    EXPECT_EQ(this->m_packets[5][2], 1U);
    EXPECT_EQ(this->m_packets[6], this->m_packets[4]);
}

void PayloadDownlinkAppTester::testRepairRetryPreservesCursor() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES * 2U] = {};
    this->writePayloadFile(payload, sizeof(payload));
    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 61, sizeof(payload));
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);

    U8 retry[] = {
        LinkCfg::PAYLOAD_MAGIC_0, LinkCfg::PAYLOAD_MAGIC_1, 4, 1, 0, 0, 1, 0x03
    };
    Fw::Buffer retryBuffer(retry, sizeof(retry));
    this->clearHistory();
    this->m_packets.clear();
    this->invoke_to_packetIn(0, retryBuffer);
    this->rejectNextPacket(2U, Components::PayloadSendStatus::LOCAL_RETRY);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 1U);
    ASSERT_EVENTS_PayloadDownlinkFailed_SIZE(0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 3U);
    EXPECT_EQ(this->m_packets[0], this->m_packets[1]);
    EXPECT_EQ(this->m_packets[2][4], 1U);
}

void PayloadDownlinkAppTester::testLocalErrorFailsWithoutAdvance() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES] = {};
    this->writePayloadFile(payload, sizeof(payload));
    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 71, sizeof(payload));
    this->component.doDispatch();
    this->clearHistory();
    this->m_packets.clear();
    this->rejectNextPacket(2U, Components::PayloadSendStatus::LOCAL_ERROR);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 4U);
    EXPECT_EQ(this->m_packets[3][2], 2U);
    EXPECT_EQ(this->m_packets[3][4], 0U);
    ASSERT_EVENTS_PayloadDownlinkFailed_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkFailed(0, 6, 13);
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(0);
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_from_statusOut(0, 4, 1, 71, sizeof(payload), 0, 1, 6);
}

void PayloadDownlinkAppTester::testInitializationFailurePublishesCorrelatedIdentity() {
    constexpr const char* missingPath = "/tmp/payload_downlink_missing_source.bin";
    (void)::unlink(missingPath);
    ASSERT_EQ(::setenv("NEUTRON_PAYLOAD_DOWNLINK_FILE", missingPath, 1), 0);
    this->clearHistory();

    Fw::String sourcePath(missingPath);
    this->invoke_to_downlinkRequestIn(
        0, 81, LinkCfg::PAYLOAD_PACKET_DATA_BYTES, Components::ScienceProductSource::TEST, sourcePath, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_PayloadDownlinkStarted_SIZE(0);
    ASSERT_EVENTS_PayloadDownlinkFailed_SIZE(1);
    ASSERT_EVENTS_PayloadDownlinkFailed(0, 7, LinkCfg::PAYLOAD_PACKET_DATA_BYTES);
    ASSERT_from_statusOut_SIZE(2);
    ASSERT_from_statusOut(
        0, 1, 1, 81, LinkCfg::PAYLOAD_PACKET_DATA_BYTES, 0, 1, 0);
    ASSERT_from_statusOut(
        1, 4, 1, 81, LinkCfg::PAYLOAD_PACKET_DATA_BYTES, 0, 1, 7);
}

void PayloadDownlinkAppTester::testRepairsDoNotStarveNominalProgress() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES * 40U] = {};
    this->writePayloadFile(payload, sizeof(payload));
    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 82, sizeof(payload));
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), LinkCfg::PAYLOAD_PACKETS_PER_RUN);

    U8 retry[] = {
        LinkCfg::PAYLOAD_MAGIC_0, LinkCfg::PAYLOAD_MAGIC_1, 4, 1, 0, 0, 3, 0xFF, 0xFF, 0x03
    };
    Fw::Buffer retryBuffer(retry, sizeof(retry));
    this->invoke_to_packetIn(0, retryBuffer);
    const std::size_t secondRunStart = this->m_packets.size();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();

    ASSERT_EQ(this->m_packets.size(), secondRunStart + LinkCfg::PAYLOAD_PACKETS_PER_RUN);
    const U32 repairBudget = LinkCfg::PAYLOAD_PACKETS_PER_RUN / 2U;
    for (U32 index = 0U; index < repairBudget; ++index) {
        ASSERT_EQ(this->m_packets[secondRunStart + index][2], 2U);
        EXPECT_EQ(this->m_packets[secondRunStart + index][4], index);
    }
    EXPECT_EQ(this->m_packets[secondRunStart + repairBudget][2], 1U);
    const std::vector<U8>& firstNominal = this->m_packets[secondRunStart + repairBudget + 1U];
    EXPECT_EQ(firstNominal[2], 2U);
    EXPECT_EQ(firstNominal[4], LinkCfg::PAYLOAD_PACKETS_PER_RUN - 3U);
}

void PayloadDownlinkAppTester::testMalformedControlIsRejectedWithoutPoisoningStatus() {
    U8 payload[LinkCfg::PAYLOAD_PACKET_DATA_BYTES] = {};
    this->writePayloadFile(payload, sizeof(payload));
    this->sendCmd_START_PAYLOAD_DOWNLINK(0, 0, 83, sizeof(payload));
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadDownlinkComplete_SIZE(1);

    U8 staleRetry[] = {
        LinkCfg::PAYLOAD_MAGIC_0, LinkCfg::PAYLOAD_MAGIC_1, 4, 99, 0, 0, 1, 0x01
    };
    Fw::Buffer staleBuffer(staleRetry, sizeof(staleRetry));
    this->clearHistory();
    this->invoke_to_packetIn(0, staleBuffer);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadControlPacketRejected_SIZE(1);
    ASSERT_EVENTS_PayloadControlPacketRejected(0, 4, 1);

    this->clearHistory();
    this->sendCmd_GET_PAYLOAD_STATUS(0, 1);
    this->component.doDispatch();
    ASSERT_EVENTS_PayloadStatus_SIZE(1);
    ASSERT_EVENTS_PayloadStatus(0, 2, 1, 1, 0);
}

}  // namespace Components
