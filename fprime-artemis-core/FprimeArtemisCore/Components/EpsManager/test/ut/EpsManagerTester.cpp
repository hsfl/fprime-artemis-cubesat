#include "EpsManagerTester.hpp"

namespace Components {
EpsManagerTester::EpsManagerTester() : EpsManagerGTestBase("EpsManagerTester", MAX_HISTORY_SIZE), component("eps") {
    initComponents(); connectPorts(); component.configure(10, 0);
}
Drv::ByteStreamStatus EpsManagerTester::from_drvSendOut_handler(FwIndexType, Fw::Buffer& buffer) {
    pushFromPortEntry_drvSendOut(buffer);
    request.assign(buffer.getData(), buffer.getData() + buffer.getSize());
    return Drv::ByteStreamStatus::OP_OK;
}
void EpsManagerTester::tick(unsigned count) {
    for (unsigned i = 0; i < count; ++i) invoke_to_run(0, 0);
}
void EpsManagerTester::reply(const std::vector<U8>& payload, U8 status, bool corrupt) {
    ASSERT_GE(request.size(), PDU_V2_HEADER_LEN + PDU_V2_CRC_LEN);
    std::vector<U8> bytes = {PDU_V2_SOF, PDU_V2_VERSION, PDU_V2_MSG_RESPONSE, request[3], request[4], status,
                             static_cast<U8>(payload.size())};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const U16 crc = Artemis::Eps::PduClient::crc16(bytes.data() + 1, bytes.size() - 1);
    bytes.push_back(static_cast<U8>(crc)); bytes.push_back(static_cast<U8>(crc >> 8U));
    if (corrupt) bytes.back() ^= 1;
    Fw::Buffer buffer(bytes.data(), static_cast<FwSizeType>(bytes.size()));
    invoke_to_drvReceiveIn(0, buffer, Drv::ByteStreamStatus::OP_OK);
}
void EpsManagerTester::nominalAndBusy() {
    sendCmd_PING(TEST_INSTANCE_ID, 1);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EpsManager::OPCODE_PING, 1, Fw::CmdResponse::EXECUTION_ERROR);
    clearHistory();
    invoke_to_drvConnected(0);
    sendCmd_PING(TEST_INSTANCE_ID, 2);
    ASSERT_CMD_RESPONSE_SIZE(0);
    ASSERT_EQ(request[3], PDU_V2_OP_PING);
    sendCmd_GET_SUMMARY_STATUS(TEST_INSTANCE_ID, 3);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EpsManager::OPCODE_GET_SUMMARY_STATUS, 3, Fw::CmdResponse::BUSY);
    reply({PDU_V2_VERSION});
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, EpsManager::OPCODE_PING, 2, Fw::CmdResponse::OK);
    ASSERT_from_drvReceiveReturnOut_SIZE(1);
}
void EpsManagerTester::readbackAndCycle() {
    invoke_to_drvConnected(0);
    sendCmd_SET_OUTPUT_STATE(TEST_INSTANCE_ID, 1, PDU_OUTPUT_BURN1, Fw::On::ON);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EpsManager::OPCODE_SET_OUTPUT_STATE, 1, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_from_drvSendOut_SIZE(0);
    clearHistory();
    sendCmd_SET_OUTPUT_STATE(TEST_INSTANCE_ID, 2, PDU_OUTPUT_3V3_1, Fw::On::ON);
    ASSERT_EQ(request[3], PDU_V2_OP_SET_OUTPUT_STATE);
    reply({PDU_OUTPUT_3V3_1, 1});
    ASSERT_CMD_RESPONSE(0, EpsManager::OPCODE_SET_OUTPUT_STATE, 2, Fw::CmdResponse::OK);
    EpsStatus status;
    invoke_to_readingGet(0, status);
    ASSERT_EQ(status.get_knownOutputs() & 1U, 1U);
    ASSERT_EQ(status.get_outputBitmap() & 1U, 1U);
    clearHistory();
    sendCmd_SET_OUTPUT_STATE(TEST_INSTANCE_ID, 3, PDU_OUTPUT_3V3_1, Fw::On::ON);
    reply({PDU_OUTPUT_3V3_1, 0});
    ASSERT_CMD_RESPONSE(0, EpsManager::OPCODE_SET_OUTPUT_STATE, 3, Fw::CmdResponse::EXECUTION_ERROR);
    invoke_to_readingGet(0, status);
    ASSERT_EQ(status.get_knownOutputs() & 1U, 1U); // Observed OFF remains known; requested ON failed.
    ASSERT_EQ(status.get_outputBitmap() & 1U, 0U);
    clearHistory();
    sendCmd_POWER_CYCLE_OUTPUT(TEST_INSTANCE_ID, 4, PDU_OUTPUT_3V3_1, 200);
    reply({PDU_OUTPUT_3V3_1, 0});
    ASSERT_CMD_RESPONSE_SIZE(0);
    tick(25);
    ASSERT_EQ(request[3], PDU_V2_OP_GET_OUTPUT_STATE);
    reply({PDU_OUTPUT_3V3_1, 1});
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EpsManager::OPCODE_POWER_CYCLE_OUTPUT, 4, Fw::CmdResponse::OK);
    ASSERT_EVENTS_CycleStarted_SIZE(1);
    ASSERT_EVENTS_CycleRestored_SIZE(1);
}
void EpsManagerTester::faultsAndStaleness() {
    invoke_to_drvConnected(0);
    sendCmd_PING(TEST_INSTANCE_ID, 1);
    reply({PDU_V2_VERSION}, PDU_V2_STATUS_OK, true);
    ASSERT_CMD_RESPONSE_SIZE(0);
    tick(100);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EpsManager::OPCODE_PING, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_TLM_CrcErrors_SIZE(1);
    clearHistory();
    sendCmd_GET_SUMMARY_STATUS(TEST_INSTANCE_ID, 2);
    reply({1, 0, 0x20, 0, 0xD2, 4, 0, 0, PDU_V2_CAP_SUMMARY_STATUS});
    EpsStatus status;
    invoke_to_readingGet(0, status);
    ASSERT_EQ(status.get_outputBitmap(), 1U);
    ASSERT_EQ(status.get_knownOutputs(), (1U << PDU_V2_OUTPUT_COUNT) - 1U);
    ASSERT_EQ(status.get_resetCause(), 0x20U);
    ASSERT_EQ(status.get_uptimeSeconds(), 1234U);
    ASSERT_EQ(status.get_linkState(), EpsLinkState::RESPONDING);
    tick(500);
    invoke_to_readingGet(0, status);
    ASSERT_EQ(status.get_knownOutputs(), 0U);
    ASSERT_EQ(status.get_linkState(), EpsLinkState::UNKNOWN);
}
}
