#include "EpsDriver_ArtemisTester.hpp"

namespace Components {

namespace {
constexpr U8 LINK_REQUEST_QUEUED = 1;
constexpr U8 LINK_PROTOCOL_OK = 2;
constexpr U8 LINK_ERROR = 3;
constexpr U8 LINK_BUSY = 4;
constexpr U8 DRIVER_STATUS_MALFORMED_PAYLOAD = 0xFA;
}

EpsDriver_ArtemisTester::EpsDriver_ArtemisTester()
    : EpsDriver_ArtemisGTestBase("EpsDriver_ArtemisTester", MAX_HISTORY_SIZE),
      component("EpsDriver_Artemis"),
      m_requests() {
    this->initComponents();
    this->connectPorts();
}

EpsDriver_ArtemisTester::~EpsDriver_ArtemisTester() {
    this->component.deinit();
}

void EpsDriver_ArtemisTester::from_teensyRequestOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const U8* data = fwBuffer.getData();
    this->m_requests.push_back(std::vector<U8>(data, data + fwBuffer.getSize()));
    this->pushFromPortEntry_teensyRequestOut(fwBuffer);
}

void EpsDriver_ArtemisTester::testTimeoutClearsPendingRequest() {
    this->clearHistory();
    this->m_requests.clear();

    this->invoke_to_requestIn(0, Components::EpsRequest::PING, 0, 0, 0);

    ASSERT_EVENTS_PduRequestQueued_SIZE(1);
    ASSERT_from_teensyRequestOut_SIZE(1);
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_EQ(this->m_requests.size(), 1U);
    ASSERT_GE(this->m_requests[0].size(), 13U);
    EXPECT_EQ(this->m_requests[0][0], LinkCfg::TEENSY_TARGET_PDU);
    EXPECT_EQ(this->m_requests[0][1], 1U);
    EXPECT_EQ(this->m_requests[0][2], 9U);
    EXPECT_EQ(this->m_requests[0][4], PDU_V2_SOF);
    EXPECT_EQ(this->m_requests[0][5], PDU_V2_VERSION);
    EXPECT_EQ(this->m_requests[0][6], PDU_V2_MSG_REQUEST);
    EXPECT_EQ(this->m_requests[0][7], PDU_V2_OP_PING);
    ASSERT_from_statusOut(
        0,
        Components::HealthState::UNKNOWN,
        LINK_REQUEST_QUEUED,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        PDU_V2_OP_PING,
        Components::EpsRequest::PING,
        0,
        0,
        0,
        0,
        Fw::String("000000000000"));

    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PduRequestTimedOut_SIZE(0);
    ASSERT_TLM_PendingRequestTicks(1, 1);

    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PduRequestTimedOut_SIZE(1);
    ASSERT_EVENTS_PduRequestFailed_SIZE(1);
    ASSERT_from_statusOut_SIZE(2);
    ASSERT_from_statusOut(
        1,
        Components::HealthState::UNKNOWN,
        LINK_ERROR,
        0,
        0,
        0,
        0,
        0,
        0,
        LinkCfg::TEENSY_STATUS_TIMEOUT,
        PDU_V2_OP_PING,
        Components::EpsRequest::PING,
        0,
        0,
        0,
        0,
        Fw::String("000000000000"));
    ASSERT_TLM_PendingRequestTicks(2, 0);
    ASSERT_TLM_TransportFailureCount(2, 1);

    this->invoke_to_requestIn(0, Components::EpsRequest::GET_PROTOCOL_INFO, 0, 0, 0);
    ASSERT_EVENTS_PduRequestQueued_SIZE(2);
    ASSERT_from_teensyRequestOut_SIZE(2);
    ASSERT_EQ(this->m_requests.size(), 2U);
    EXPECT_EQ(this->m_requests[1][1], 2U);
    EXPECT_EQ(this->m_requests[1][7], PDU_V2_OP_GET_PROTOCOL_INFO);
}

void EpsDriver_ArtemisTester::testBusyRequestDoesNotReplacePendingIdentity() {
    this->clearHistory();
    this->m_requests.clear();
    this->invoke_to_requestIn(0, Components::EpsRequest::GET_SUMMARY_STATUS, 0, 0, 0);
    this->invoke_to_requestIn(0, Components::EpsRequest::GET_PROTOCOL_INFO, 0, 0, 0);

    ASSERT_EQ(this->m_requests.size(), 1U);
    ASSERT_from_statusOut_SIZE(2);
    ASSERT_from_statusOut(
        1,
        Components::HealthState::UNKNOWN,
        LINK_BUSY,
        0,
        0,
        0,
        0,
        0,
        0,
        0xFDU,
        PDU_V2_OP_GET_PROTOCOL_INFO,
        Components::EpsRequest::GET_PROTOCOL_INFO,
        0,
        0,
        0,
        0,
        Fw::String("000000000000"));

    this->deliverResponse(PDU_V2_OP_GET_SUMMARY_STATUS, PDU_V2_STATUS_OK,
                          std::vector<U8>{0x05, 0x00, 0x02, 0x00, 0x78, 0x56, 0x34, 0x12, 0x0F});
    ASSERT_from_statusOut_SIZE(3);
    ASSERT_from_statusOut(
        2,
        Components::HealthState::OK,
        LINK_PROTOCOL_OK,
        PDU_V2_VERSION,
        0x0005,
        0x02,
        0x00,
        0x12345678,
        0x0F,
        PDU_V2_STATUS_OK,
        PDU_V2_OP_GET_SUMMARY_STATUS,
        Components::EpsRequest::GET_SUMMARY_STATUS,
        0,
        0,
        0,
        0,
        Fw::String("000000000000"));
    ASSERT_TLM_LastRequest(2, Components::EpsRequest::GET_SUMMARY_STATUS);
}

void EpsDriver_ArtemisTester::testOutputReadbackValidationAndBitmapMapping() {
    this->clearHistory();
    this->m_requests.clear();
    this->invoke_to_requestIn(0, Components::EpsRequest::GET_OUTPUT_STATE, PDU_V2_OUTPUT_ALL, 0, 0);
    this->deliverResponse(PDU_V2_OP_GET_OUTPUT_STATE, PDU_V2_STATUS_OK,
                          std::vector<U8>{PDU_V2_OUTPUT_ALL, PDU_V2_OUTPUT_COUNT, 1, 0, 1, 0, 0, 1, 0, 0, 1});
    const U16 expectedBitmap = static_cast<U16>((1U << 0U) | (1U << 2U) | (1U << 5U) | (1U << 8U));
    ASSERT_EVENTS_PduRailReadback_SIZE(9);
    ASSERT_from_railSnapshotOut(0, expectedBitmap, 0x1FFU, 0x1FFU, 0);
    ASSERT_from_statusOut_SIZE(2);
    ASSERT_from_statusOut(
        1,
        Components::HealthState::OK,
        LINK_PROTOCOL_OK,
        0,
        expectedBitmap,
        0,
        0,
        0,
        0,
        PDU_V2_STATUS_OK,
        PDU_V2_OP_GET_OUTPUT_STATE,
        Components::EpsRequest::GET_OUTPUT_STATE,
        0,
        0,
        0,
        0,
        Fw::String("000000000000"));

    this->invoke_to_requestIn(0, Components::EpsRequest::SET_OUTPUT_STATE, PDU_OUTPUT_12V, 1, 0);
    this->deliverResponse(PDU_V2_OP_SET_OUTPUT_STATE, PDU_V2_STATUS_OK, std::vector<U8>{PDU_OUTPUT_12V, 1});
    ASSERT_EVENTS_PduRailSetResult_SIZE(1);
    ASSERT_from_statusOut(
        3,
        Components::HealthState::OK,
        LINK_PROTOCOL_OK,
        0,
        expectedBitmap,
        0,
        0,
        0,
        0,
        PDU_V2_STATUS_OK,
        PDU_V2_OP_SET_OUTPUT_STATE,
        Components::EpsRequest::SET_OUTPUT_STATE,
        0,
        0,
        0,
        0,
        Fw::String("000000000000"));

    this->invoke_to_requestIn(0, Components::EpsRequest::POWER_CYCLE_OUTPUT, PDU_OUTPUT_12V, 0, 500);
    this->deliverResponse(PDU_V2_OP_POWER_CYCLE_OUTPUT, PDU_V2_STATUS_OK, std::vector<U8>{PDU_OUTPUT_12V, 0});
    ASSERT_from_statusOut(
        5,
        Components::HealthState::OK,
        LINK_PROTOCOL_OK,
        0,
        static_cast<U16>(expectedBitmap & static_cast<U16>(~(1U << 5U))),
        0,
        0,
        0,
        0,
        PDU_V2_STATUS_OK,
        PDU_V2_OP_POWER_CYCLE_OUTPUT,
        Components::EpsRequest::POWER_CYCLE_OUTPUT,
        0,
        0,
        0,
        0,
        Fw::String("000000000000"));

    this->invoke_to_requestIn(0, Components::EpsRequest::GET_OUTPUT_STATE, PDU_OUTPUT_12V, 0, 0);
    this->deliverResponse(PDU_V2_OP_GET_OUTPUT_STATE, PDU_V2_STATUS_OK, std::vector<U8>{PDU_OUTPUT_12V, 2});
    ASSERT_from_statusOut_SIZE(8);
    ASSERT_from_statusOut(
        7,
        Components::HealthState::UNKNOWN,
        LINK_ERROR,
        0,
        static_cast<U16>(expectedBitmap & static_cast<U16>(~(1U << 5U))),
        0,
        0,
        0,
        0,
        DRIVER_STATUS_MALFORMED_PAYLOAD,
        PDU_V2_OP_GET_OUTPUT_STATE,
        Components::EpsRequest::GET_OUTPUT_STATE,
        0,
        0,
        0,
        0,
        Fw::String("000000000000"));
}

void EpsDriver_ArtemisTester::testReadableEventsAndFreshness() {
    this->clearHistory();
    this->invoke_to_run(0, 0);
    ASSERT_from_railSnapshotOut(0, 0, 0, 0, 0);
    this->invoke_to_requestIn(0, Components::EpsRequest::GET_OUTPUT_STATE, PDU_OUTPUT_5V_1, 0, 0);
    this->deliverResponse(PDU_V2_OP_GET_OUTPUT_STATE, PDU_V2_STATUS_OK, {PDU_OUTPUT_5V_1, 1});
    ASSERT_EVENTS_PduRailReadback_SIZE(1);
    ASSERT_from_railSnapshotOut(1, 4, 4, 4, 0);
    for (unsigned i = 0; i < 4; ++i) { this->invoke_to_run(0, 0); }
    ASSERT_from_railSnapshotOut(5, 4, 4, 0, 4);

    this->invoke_to_requestIn(0, Components::EpsRequest::SET_OUTPUT_STATE, PDU_OUTPUT_5V_1, 1, 0);
    this->deliverResponse(PDU_V2_OP_SET_OUTPUT_STATE, PDU_V2_STATUS_OK, {PDU_OUTPUT_5V_1, 0});
    ASSERT_EVENTS_PduRailSetResult_SIZE(0);
    ASSERT_EVENTS_PduRailSetMismatch_SIZE(1);
    ASSERT_EVENTS_PduOperationError_SIZE(1);
    ASSERT_from_railSnapshotOut(6, 0, 4, 4, 0);

    this->invoke_to_requestIn(0, Components::EpsRequest::GET_OUTPUT_STATE, PDU_V2_OUTPUT_ALL, 0, 0);
    this->deliverResponse(PDU_V2_OP_GET_OUTPUT_STATE, PDU_V2_STATUS_OK,
                          {PDU_V2_OUTPUT_ALL, PDU_V2_OUTPUT_COUNT, 1, 0, 0, 0, 0, 0, 0, 0, 2});
    ASSERT_from_railSnapshotOut(7, 0, 4, 0, 0);
    ASSERT_EVENTS_PduRailReadback_SIZE(1);
}

void EpsDriver_ArtemisTester::testLegacyAndExtendedProtocolInfo() {
    this->clearHistory();
    this->m_requests.clear();

    this->invoke_to_requestIn(0, Components::EpsRequest::GET_PROTOCOL_INFO, 0, 0, 0);
    this->deliverResponse(PDU_V2_OP_GET_PROTOCOL_INFO, PDU_V2_STATUS_OK,
                          std::vector<U8>{PDU_V2_VERSION, 0x0F, 96, PDU_V2_OUTPUT_COUNT, 1, 2, 3});
    ASSERT_EVENTS_PduInfoReadback_SIZE(1);
    ASSERT_from_statusOut(
        1,
        Components::HealthState::OK,
        LINK_PROTOCOL_OK,
        PDU_V2_VERSION,
        0,
        0,
        0,
        0,
        0x0F,
        PDU_V2_STATUS_OK,
        PDU_V2_OP_GET_PROTOCOL_INFO,
        Components::EpsRequest::GET_PROTOCOL_INFO,
        1,
        2,
        3,
        0,
        Fw::String("000000000000"));

    std::vector<U8> extended(PDU_V2_PROTOCOL_INFO_EXT_LEN, 0);
    extended[0] = PDU_V2_VERSION;
    extended[1] = 0x0F;
    extended[2] = 96;
    extended[3] = PDU_V2_OUTPUT_COUNT;
    extended[4] = 4;
    extended[5] = 5;
    extended[6] = 6;
    extended[PDU_V2_PROTOCOL_INFO_FLAGS_OFFSET] = 0;
    this->invoke_to_requestIn(0, Components::EpsRequest::GET_PROTOCOL_INFO, 0, 0, 0);
    this->deliverResponse(PDU_V2_OP_GET_PROTOCOL_INFO, PDU_V2_STATUS_OK, extended);
    ASSERT_from_statusOut(
        3,
        Components::HealthState::OK,
        LINK_PROTOCOL_OK,
        PDU_V2_VERSION,
        0,
        0,
        0,
        0,
        0x0F,
        PDU_V2_STATUS_OK,
        PDU_V2_OP_GET_PROTOCOL_INFO,
        Components::EpsRequest::GET_PROTOCOL_INFO,
        4,
        5,
        6,
        0,
        Fw::String("000000000000"));

    extended[4] = 7;
    extended[5] = 8;
    extended[6] = 9;
    extended[PDU_V2_PROTOCOL_INFO_FLAGS_OFFSET] =
        PDU_V2_PROTOCOL_INFO_FLAG_HASH_KNOWN | PDU_V2_PROTOCOL_INFO_FLAG_DIRTY;
    const char hash[] = "deadbeef0123";
    for (U8 index = 0; index < PDU_V2_PROTOCOL_INFO_HASH_LEN; ++index) {
        extended[PDU_V2_PROTOCOL_INFO_HASH_OFFSET + index] = static_cast<U8>(hash[index]);
    }
    this->invoke_to_requestIn(0, Components::EpsRequest::GET_PROTOCOL_INFO, 0, 0, 0);
    this->deliverResponse(PDU_V2_OP_GET_PROTOCOL_INFO, PDU_V2_STATUS_OK, extended);
    ASSERT_from_statusOut(
        5,
        Components::HealthState::OK,
        LINK_PROTOCOL_OK,
        PDU_V2_VERSION,
        0,
        0,
        0,
        0,
        0x0F,
        PDU_V2_STATUS_OK,
        PDU_V2_OP_GET_PROTOCOL_INFO,
        Components::EpsRequest::GET_PROTOCOL_INFO,
        7,
        8,
        9,
        (PDU_V2_PROTOCOL_INFO_FLAG_HASH_KNOWN | PDU_V2_PROTOCOL_INFO_FLAG_DIRTY),
        Fw::String("deadbeef0123"));
}

void EpsDriver_ArtemisTester::deliverResponse(U8 opcode, U8 status, const std::vector<U8>& payload) {
    ASSERT_FALSE(this->m_requests.empty());
    const U8 sequence = this->m_requests.back()[1];
    const U32 frameLen = PDU_V2_HEADER_LEN + payload.size() + PDU_V2_CRC_LEN;
    std::vector<U8> frame(frameLen, 0);
    frame[0] = PDU_V2_SOF;
    frame[1] = PDU_V2_VERSION;
    frame[2] = PDU_V2_MSG_RESPONSE;
    frame[3] = opcode;
    frame[4] = sequence;
    frame[5] = status;
    frame[6] = static_cast<U8>(payload.size());
    for (std::size_t index = 0; index < payload.size(); ++index) {
        frame[PDU_V2_HEADER_LEN + index] = payload[index];
    }
    const U16 crc = crc16Ccitt(frame.data() + 1, 6U + static_cast<U32>(payload.size()));
    frame[frameLen - 2U] = static_cast<U8>(crc & 0xFFU);
    frame[frameLen - 1U] = static_cast<U8>((crc >> 8U) & 0xFFU);

    std::vector<U8> local(4U + frameLen, 0);
    local[0] = LinkCfg::TEENSY_TARGET_PDU;
    local[1] = sequence;
    local[2] = LinkCfg::TEENSY_STATUS_OK;
    local[3] = static_cast<U8>(frameLen);
    for (U32 index = 0; index < frameLen; ++index) {
        local[4U + index] = frame[index];
    }
    Fw::Buffer response(local.data(), static_cast<FwSizeType>(local.size()));
    this->invoke_to_teensyResponseIn(0, response);
}

U16 EpsDriver_ArtemisTester::crc16Ccitt(const U8* data, U32 length) {
    U16 crc = 0xFFFFU;
    for (U32 index = 0; index < length; ++index) {
        crc ^= static_cast<U16>(data[index] << 8U);
        for (U32 bit = 0; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) != 0U ? static_cast<U16>((crc << 1U) ^ 0x1021U) :
                                         static_cast<U16>(crc << 1U);
        }
    }
    return crc;
}

}  // namespace Components
