#include "PayloadStreamAppTester.hpp"

#include <cstring>

namespace Components {

PayloadStreamAppTester::PayloadStreamAppTester()
    : PayloadStreamAppGTestBase("PayloadStreamAppTester", MAX_HISTORY_SIZE),
      component("PayloadStreamApp"),
      m_preview{},
      m_packets() {
    for (FwSizeType i = 0U; i < sizeof(this->m_preview); ++i) {
        this->m_preview[i] = static_cast<U8>(i & 0xFFU);
    }
    this->initComponents();
    this->connectPorts();
}

PayloadStreamAppTester::~PayloadStreamAppTester() {
    this->component.deinit();
}

void PayloadStreamAppTester::from_previewRequestOut_handler(FwIndexType portNum) {
    static_cast<void>(portNum);
    Fw::Buffer preview(this->m_preview, sizeof(this->m_preview));
    this->invoke_to_previewIn(0, preview);
    this->pushFromPortEntry_previewRequestOut();
}

void PayloadStreamAppTester::from_responseAdvanceOut_handler(FwIndexType portNum) {
    static_cast<void>(portNum);
    this->invoke_to_responseAdvanceIn(0);
    this->pushFromPortEntry_responseAdvanceOut();
}

Components::PayloadSendStatus PayloadStreamAppTester::from_previewPacketOut_handler(FwIndexType portNum,
                                                                                      Fw::Buffer& packet) {
    static_cast<void>(portNum);
    this->m_packets.push_back(std::vector<U8>(packet.getData(), packet.getData() + packet.getSize()));
    this->pushFromPortEntry_previewPacketOut(packet);
    return Components::PayloadSendStatus::LOCAL_ACCEPTED;
}

void PayloadStreamAppTester::respondToLastRequest(U8 status, U32 receivedBytes) {
    ASSERT_FALSE(this->m_packets.empty());
    const std::vector<U8>& request = this->m_packets.back();
    ASSERT_GE(request.size(), 7U);
    U8 response[12] = {};
    response[0] = LinkCfg::TEENSY_TARGET_LEPTON_PREVIEW;
    response[1] = request[1];
    response[2] = status;
    response[3] = 8U;
    response[4] = request[4];
    response[5] = request[5];
    response[6] = request[6];
    response[7] = 2U;
    response[8] = static_cast<U8>(receivedBytes & 0xFFU);
    response[9] = static_cast<U8>((receivedBytes >> 8U) & 0xFFU);
    response[10] = static_cast<U8>((receivedBytes >> 16U) & 0xFFU);
    response[11] = static_cast<U8>((receivedBytes >> 24U) & 0xFFU);
    Fw::Buffer buffer(response, sizeof(response));
    this->invoke_to_previewResponseIn(0, buffer);
    this->component.doDispatch();
}

void PayloadStreamAppTester::testResponsePacedUploadStartsNextFrameImmediately() {
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_STREAM(0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(0, PayloadStreamAppComponentBase::OPCODE_START_STREAM, 0, Fw::CmdResponse::OK);
    ASSERT_from_previewRequestOut_SIZE(1);
    // previewIn is queued so the stream component owns all upload state.
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 1U);
    ASSERT_EQ(this->m_packets[0].size(), 18U);
    EXPECT_EQ(this->m_packets[0][0], LinkCfg::TEENSY_TARGET_LEPTON_PREVIEW);
    EXPECT_EQ(this->m_packets[0][2], 14U);
    EXPECT_EQ(this->m_packets[0][4], 1U);
    EXPECT_EQ(this->m_packets[0][11], 80U);
    EXPECT_EQ(this->m_packets[0][12], 60U);
    EXPECT_EQ(this->m_packets[0][13], 1U);
    EXPECT_EQ(this->m_packets[0][14], 0xC0U);
    EXPECT_EQ(this->m_packets[0][15], 0x12U);

    this->respondToLastRequest(0U, 0U);
    for (U32 chunk = 0U; chunk < 24U; ++chunk) {
        ASSERT_EQ(this->m_packets.back()[4], 2U);
        ASSERT_LE(this->m_packets.back()[9], 200U);
        this->respondToLastRequest(0U, (chunk + 1U) * 200U);
    }
    ASSERT_EQ(this->m_packets.back()[4], 3U);
    this->respondToLastRequest(0U, 4800U);

    ASSERT_EVENTS_PreviewUploaded_SIZE(1);
    ASSERT_EVENTS_PreviewUploaded(0, 1, 1);
    ASSERT_EQ(this->m_packets.size(), 26U);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 27U);
    EXPECT_EQ(this->m_packets.back()[4], 1U);
    ASSERT_from_previewRequestOut_SIZE(2);
    ASSERT_TLM_FramesUploaded(1, 1U);
}

void PayloadStreamAppTester::testTargetFailureDropsWithoutRetry() {
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_STREAM(0, 0);
    this->component.doDispatch();
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 1U);
    this->respondToLastRequest(4U, 0U);

    ASSERT_EVENTS_PreviewDropped_SIZE(1);
    ASSERT_EVENTS_PreviewDropped(0, 2U, 4U);
    // The failed frame is not retried; streaming advances to a new source
    // request, whose queued preview then begins a distinct session.
    ASSERT_from_previewRequestOut_SIZE(2);
    ASSERT_EQ(this->m_packets.size(), 1U);
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 2U);
    EXPECT_EQ(this->m_packets.back()[4], 1U);
    EXPECT_NE(this->m_packets.back()[5], this->m_packets.front()[5]);
    ASSERT_TLM_FramesDropped(1, 1U);
}

void PayloadStreamAppTester::testLostResponseTimesOutAndAdvancesToNewFrame() {
    this->clearHistory();
    this->m_packets.clear();

    this->sendCmd_START_STREAM(0, 0);
    this->component.doDispatch();
    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 1U);

    // A timely successful response restarts the full five-second deadline
    // for the resulting CHUNK request.
    for (U32 tick = 0U; tick < 4U; ++tick) {
        this->invoke_to_run(0, 0U);
        this->component.doDispatch();
    }
    this->respondToLastRequest(0U, 0U);
    ASSERT_EQ(this->m_packets.size(), 2U);
    ASSERT_EQ(this->m_packets.back()[4], 2U);

    for (U32 tick = 0U; tick < 4U; ++tick) {
        this->invoke_to_run(0, 0U);
        this->component.doDispatch();
    }
    ASSERT_EVENTS_PreviewDropped_SIZE(0);
    ASSERT_EQ(this->m_packets.size(), 2U);

    this->invoke_to_run(0, 0U);
    this->component.doDispatch();
    ASSERT_EVENTS_PreviewDropped_SIZE(1);
    ASSERT_EVENTS_PreviewDropped(0, 6U, 5U);
    ASSERT_from_previewRequestOut_SIZE(2);
    ASSERT_EQ(this->m_packets.size(), 2U);

    this->component.doDispatch();
    ASSERT_EQ(this->m_packets.size(), 3U);
    EXPECT_EQ(this->m_packets.back()[4], 1U);
    EXPECT_NE(this->m_packets.back()[5], this->m_packets.front()[5]);
}

}  // namespace Components
