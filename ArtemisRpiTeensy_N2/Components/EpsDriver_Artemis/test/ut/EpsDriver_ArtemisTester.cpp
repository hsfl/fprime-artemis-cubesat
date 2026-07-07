#include "EpsDriver_ArtemisTester.hpp"

namespace Components {

namespace {
constexpr U8 LINK_REQUEST_QUEUED = 1;
constexpr U8 LINK_ERROR = 3;
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
        PDU_V2_OP_PING);

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
        PDU_V2_OP_PING);
    ASSERT_TLM_PendingRequestTicks(2, 0);
    ASSERT_TLM_TransportFailureCount(2, 1);

    this->invoke_to_requestIn(0, Components::EpsRequest::GET_PROTOCOL_INFO, 0, 0, 0);
    ASSERT_EVENTS_PduRequestQueued_SIZE(2);
    ASSERT_from_teensyRequestOut_SIZE(2);
    ASSERT_EQ(this->m_requests.size(), 2U);
    EXPECT_EQ(this->m_requests[1][1], 2U);
    EXPECT_EQ(this->m_requests[1][7], PDU_V2_OP_GET_PROTOCOL_INFO);
}

}  // namespace Components
