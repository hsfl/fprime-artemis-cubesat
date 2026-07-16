#include "CommsDriver_TeensyRfm23Tester.hpp"

namespace Components {

namespace {
constexpr FwSizeType LOCAL_HEADER_LEN = 4U;
constexpr U8 STATUS_PAYLOAD_LEN = 33U;
constexpr U8 SET_ENABLED_PAYLOAD_LEN = 4U;
}

CommsDriver_TeensyRfm23Tester::CommsDriver_TeensyRfm23Tester()
    : CommsDriver_TeensyRfm23GTestBase("CommsDriver_TeensyRfm23Tester", MAX_HISTORY_SIZE),
      component("CommsDriver_TeensyRfm23"),
      m_requests() {
    this->initComponents();
    this->connectPorts();
}

CommsDriver_TeensyRfm23Tester::~CommsDriver_TeensyRfm23Tester() {
    this->component.deinit();
}

void CommsDriver_TeensyRfm23Tester::from_teensyRequestOut_handler(FwIndexType portNum,
                                                                  Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const U8* data = fwBuffer.getData();
    this->m_requests.push_back(std::vector<U8>(data, data + fwBuffer.getSize()));
    this->pushFromPortEntry_teensyRequestOut(fwBuffer);
}

void CommsDriver_TeensyRfm23Tester::testParsesCorrelatedStatus() {
    this->clearHistory();
    this->m_requests.clear();

    this->invoke_to_requestIn(0, Components::RadioOperation::STATUS, 0U);
    ASSERT_EQ(this->m_requests.size(), 1U);
    ASSERT_EQ(this->m_requests[0].size(), 5U);
    EXPECT_EQ(this->m_requests[0][0], LinkCfg::TEENSY_TARGET_RF_STATUS);
    EXPECT_EQ(this->m_requests[0][2], 1U);
    EXPECT_EQ(this->m_requests[0][4], LinkCfg::TEENSY_RF_OP_STATUS);

    U8 response[LOCAL_HEADER_LEN + STATUS_PAYLOAD_LEN] = {};
    response[0] = LinkCfg::TEENSY_TARGET_RF_STATUS;
    response[1] = this->m_requests[0][1];
    response[2] = LinkCfg::TEENSY_STATUS_OK;
    response[3] = STATUS_PAYLOAD_LEN;
    response[4] = LinkCfg::TEENSY_RF_OP_STATUS;
    writeLe16(&response[5], static_cast<U16>(static_cast<I16>(-72)));
    writeLe32(&response[13], 101U);
    writeLe32(&response[17], 202U);
    writeLe32(&response[21], 3U);
    response[25] = LinkCfg::TEENSY_RF_STATE_READY;
    response[26] = LinkCfg::TEENSY_RF_FAULT_NONE;
    response[27] = LinkCfg::TEENSY_RF_BOOT_FLAG_WATCHDOG;
    response[28] = 1U;
    writeLe32(&response[29], 250U);
    writeLe32(&response[33], 4U);
    Fw::Buffer buffer(response, sizeof(response));
    this->invoke_to_teensyResponseIn(0, buffer);

    ASSERT_from_statusOut_SIZE(1);
    ASSERT_from_statusOut(0,
                          Components::RadioOperation::STATUS,
                          Components::RadioRpcResult::OK,
                          Components::RadioState::READY,
                          Components::RadioFault::NONE,
                          LinkCfg::TEENSY_RF_BOOT_FLAG_WATCHDOG,
                          1U,
                          -72,
                          250U,
                          4U,
                          101U,
                          202U,
                          3U);
    ASSERT_TLM_RequestPending(1, 0U);
    ASSERT_TLM_RssiDbm(0, -72);
    ASSERT_TLM_RadioInitAttempts(0, 4U);
}

void CommsDriver_TeensyRfm23Tester::testTargetErrorPreservesFactualOffState() {
    this->clearHistory();
    this->m_requests.clear();

    this->invoke_to_requestIn(0, Components::RadioOperation::SET_ENABLED, 1U);
    ASSERT_EQ(this->m_requests.size(), 1U);
    ASSERT_EQ(this->m_requests[0].size(), 6U);

    U8 response[LOCAL_HEADER_LEN + SET_ENABLED_PAYLOAD_LEN] = {};
    response[0] = LinkCfg::TEENSY_TARGET_RF_STATUS;
    response[1] = this->m_requests[0][1];
    response[2] = LinkCfg::TEENSY_STATUS_TARGET_ERROR;
    response[3] = SET_ENABLED_PAYLOAD_LEN;
    response[4] = LinkCfg::TEENSY_RF_OP_SET_ENABLED;
    response[5] = 1U;
    response[6] = LinkCfg::TEENSY_RF_STATE_OFF;
    response[7] = LinkCfg::TEENSY_RF_FAULT_INIT_FAILED;
    Fw::Buffer buffer(response, sizeof(response));
    this->invoke_to_teensyResponseIn(0, buffer);

    ASSERT_from_statusOut_SIZE(1);
    ASSERT_from_statusOut(0,
                          Components::RadioOperation::SET_ENABLED,
                          Components::RadioRpcResult::TARGET_ERROR,
                          Components::RadioState::OFF,
                          Components::RadioFault::INIT_FAILED,
                          0U,
                          0U,
                          0,
                          LinkCfg::TEENSY_RF_RSSI_AGE_UNKNOWN_MS,
                          0U,
                          0U,
                          0U,
                          0U);
    ASSERT_EVENTS_RadioRequestFailed_SIZE(1);
}

void CommsDriver_TeensyRfm23Tester::testRejectsStaleResponseAndTimesOutPendingRequest() {
    this->clearHistory();
    this->m_requests.clear();

    this->invoke_to_requestIn(0, Components::RadioOperation::STATUS, 0U);
    ASSERT_EQ(this->m_requests.size(), 1U);

    U8 stale[LOCAL_HEADER_LEN + STATUS_PAYLOAD_LEN] = {};
    stale[0] = LinkCfg::TEENSY_TARGET_RF_STATUS;
    stale[1] = static_cast<U8>(this->m_requests[0][1] + 1U);
    stale[2] = LinkCfg::TEENSY_STATUS_OK;
    stale[3] = STATUS_PAYLOAD_LEN;
    stale[4] = LinkCfg::TEENSY_RF_OP_STATUS;
    stale[25] = LinkCfg::TEENSY_RF_STATE_READY;
    stale[26] = LinkCfg::TEENSY_RF_FAULT_NONE;
    stale[28] = 0U;
    Fw::Buffer staleBuffer(stale, sizeof(stale));
    this->invoke_to_teensyResponseIn(0, staleBuffer);

    ASSERT_EVENTS_RadioResponseRejected_SIZE(1);
    ASSERT_from_statusOut_SIZE(0);
    ASSERT_TLM_RequestPending(0, 1U);

    for (U32 tick = 0U; tick < 15U; tick++) {
        this->invoke_to_run(0, 0U);
    }

    ASSERT_EVENTS_RadioRequestTimedOut_SIZE(1);
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_from_statusOut(0,
                          Components::RadioOperation::STATUS,
                          Components::RadioRpcResult::TIMEOUT,
                          Components::RadioState::OFF,
                          Components::RadioFault::NONE,
                          0U,
                          0U,
                          0,
                          LinkCfg::TEENSY_RF_RSSI_AGE_UNKNOWN_MS,
                          0U,
                          0U,
                          0U,
                          0U);
    ASSERT_TLM_RequestPending(1, 0U);

    this->invoke_to_requestIn(0, Components::RadioOperation::SET_ENABLED, 1U);
    ASSERT_EQ(this->m_requests.size(), 2U);
    EXPECT_NE(this->m_requests[0][1], this->m_requests[1][1]);
}

void CommsDriver_TeensyRfm23Tester::writeLe16(U8* out, U16 value) {
    out[0] = static_cast<U8>(value & 0xFFU);
    out[1] = static_cast<U8>((value >> 8U) & 0xFFU);
}

void CommsDriver_TeensyRfm23Tester::writeLe32(U8* out, U32 value) {
    out[0] = static_cast<U8>(value & 0xFFU);
    out[1] = static_cast<U8>((value >> 8U) & 0xFFU);
    out[2] = static_cast<U8>((value >> 16U) & 0xFFU);
    out[3] = static_cast<U8>((value >> 24U) & 0xFFU);
}

}  // namespace Components
