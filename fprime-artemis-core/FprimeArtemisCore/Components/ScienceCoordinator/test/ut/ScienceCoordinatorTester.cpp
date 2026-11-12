#include "ScienceCoordinatorTester.hpp"
#include <algorithm>
#include <vector>

namespace Components {
ScienceCoordinatorTester::ScienceCoordinatorTester()
    : ScienceCoordinatorGTestBase("Tester", MAX_HISTORY_SIZE), component("Coordinator") {
    initComponents(); connectPorts(); component.configure(11);
}
void ScienceCoordinatorTester::tick(unsigned count) {
    for (unsigned i = 0; i < count; ++i) invoke_to_run(0, 0);
}
void ScienceCoordinatorTester::capture(TwinScience::Frame& request) {
    invoke_to_admissionIn(0, true); invoke_to_payloadStateIn(0, PayloadState::READY);
    ASSERT_EQ(invoke_to_scheduleIn(0, 0), Fw::CmdResponse::OK);
    tick(); ASSERT_from_messageOut_SIZE(1);
    request = fromPortHistory_messageOut->at(0).frame;
    ASSERT_EQ(request.get_operation(), TwinScience::Operation::CAPTURE_REQUEST);
    ASSERT_EQ(request.get_session(), 11U); ASSERT_NE(request.get_requestId(), 0U);
    clearHistory();
}
void ScienceCoordinatorTester::stage(TwinScience::Frame& commit, unsigned size, bool corrupt) {
    TwinScience::Frame frame; capture(frame);
    std::vector<U8> bytes(size);
    for (unsigned i = 0; i < size; ++i) bytes[i] = static_cast<U8>(i * 17 + 3);
    frame.set_operation(TwinScience::Operation::PRODUCT_COMMITTED);
    frame.set_productId(42); frame.set_totalBytes(size);
    frame.set_crc(Artemis::Science::crc16Ccitt(bytes.data(), bytes.size()));
    invoke_to_messageIn(0, frame); tick(); ASSERT_from_messageOut_SIZE(1);
    frame = fromPortHistory_messageOut->at(0).frame;
    ASSERT_EQ(frame.get_operation(), TwinScience::Operation::STAGE_REQUEST);
    ASSERT_NE(frame.get_generation(), 0U);
    clearHistory(); frame.set_operation(TwinScience::Operation::STAGE_BEGIN);
    invoke_to_messageIn(0, frame); tick(); ASSERT_from_messageOut_SIZE(1);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::STAGE_ACK);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_offset(), 0U);
    if (corrupt) bytes[0] ^= 1;
    for (unsigned offset = 0; offset < size;) {
        clearHistory();
        const auto length = std::min<unsigned>(200, size - offset);
        TwinScience::Data data;
        for (unsigned i = 0; i < length; ++i) data[i] = bytes[offset + i];
        frame.set_operation(TwinScience::Operation::STAGE_CHUNK);
        frame.set_offset(offset); frame.set_length(length); frame.set_data(data);
        invoke_to_messageIn(0, frame); tick();
        ASSERT_from_messageOut_SIZE(1);
        ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::STAGE_ACK);
        offset += length;
        ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_offset(), offset);
    }
    clearHistory(); frame.set_operation(TwinScience::Operation::STAGE_COMMIT);
    frame.set_offset(size); frame.set_length(0); commit = frame;
    invoke_to_messageIn(0, frame); tick();
}
void ScienceCoordinatorTester::drainRadio() {
    ScienceBytes bytes; U8 length = 0; unsigned packets = 0;
    while (invoke_to_radioNext(0, bytes, length)) {
        ASSERT_GT(length, 0); ASSERT_LE(length, 44);
        invoke_to_radioSent(0, true);
        ASSERT_LT(++packets, 6000U);
    }
    ASSERT_GT(packets, 0U); tick();
}
void ScienceCoordinatorTester::admissionAndReadiness() {
    ASSERT_EQ(invoke_to_scheduleIn(0, 0), Fw::CmdResponse::EXECUTION_ERROR);
    invoke_to_admissionIn(0, true); invoke_to_payloadStateIn(0, PayloadState::READY);
    tick(301);
    ASSERT_EQ(invoke_to_scheduleIn(0, 0), Fw::CmdResponse::EXECUTION_ERROR);
    invoke_to_payloadStateIn(0, PayloadState::READY);
    ASSERT_EQ(invoke_to_scheduleIn(0, 86401), Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EQ(invoke_to_scheduleIn(0, 2), Fw::CmdResponse::OK);
    ASSERT_EQ(invoke_to_scheduleIn(0, 1), Fw::CmdResponse::BUSY);
    invoke_to_payloadStateIn(0, PayloadState::UNKNOWN); clearHistory(); tick(200);
    ASSERT_from_messageOut_SIZE(1); ASSERT_EVENTS_ScienceFailed_SIZE(1);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::ABORT);
}
void ScienceCoordinatorTester::completeMaximumProduct() {
    TwinScience::Frame commit; stage(commit, 196608);
    ASSERT_EVENTS_ScienceFailed_SIZE(0); ASSERT_from_messageOut_SIZE(1);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::CACHE_READY);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::OK); drainRadio();
    ASSERT_EVENTS_ScienceFailed_SIZE(0);
}
void ScienceCoordinatorTester::corruptedProductRefused() {
    TwinScience::Frame commit; stage(commit, 1001, true);
    ASSERT_EVENTS_ScienceFailed_SIZE(1);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::EXECUTION_ERROR);
}
void ScienceCoordinatorTester::duplicateCommitDuringDownlink() {
    TwinScience::Frame commit; stage(commit);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::OK);
    clearHistory(); invoke_to_messageIn(0, commit); tick();
    ASSERT_EVENTS_ScienceFailed_SIZE(0);
    ASSERT_EVENTS_ScienceProgress_SIZE(0);  // Duplicate COMMIT must not change DOWNLINKING to CACHE_READY/FAILED.
    ASSERT_from_messageOut_SIZE(1);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::CACHE_READY);
    drainRadio();
}
void ScienceCoordinatorTester::staleErrorAfterSend() {
    TwinScience::Frame commit; stage(commit);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::OK); drainRadio(); clearHistory();
    commit.set_operation(TwinScience::Operation::ERROR); commit.set_error(7);
    invoke_to_messageIn(0, commit); tick();
    ASSERT_EVENTS_ScienceFailed_SIZE(0); ASSERT_EVENTS_ScienceProgress_SIZE(0);
}
void ScienceCoordinatorTester::cancelRetainsPiIndependentCache() {
    TwinScience::Frame commit; stage(commit);
    ASSERT_EQ(invoke_to_cancelIn(0), Fw::CmdResponse::OK);
    invoke_to_payloadStateIn(0, PayloadState::UNKNOWN); invoke_to_admissionIn(0, false);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::OK); clearHistory(); drainRadio();
    ASSERT_EVENTS_ScienceFailed_SIZE(0);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::OK);  // Retained cache is reusable.
}
void ScienceCoordinatorTester::lateProductAfterTimeout() {
    TwinScience::Frame frame; capture(frame); tick(3301);
    ASSERT_EVENTS_ScienceFailed_SIZE(1); clearHistory();
    frame.set_operation(TwinScience::Operation::PRODUCT_COMMITTED);
    frame.set_productId(42); frame.set_totalBytes(9); frame.set_crc(0x29B1);
    invoke_to_messageIn(0, frame); tick();
    ASSERT_from_messageOut_SIZE(0); ASSERT_EVENTS_ScienceProgress_SIZE(0);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::EXECUTION_ERROR);
}
void ScienceCoordinatorTester::localRadioFailureIsBounded() {
    TwinScience::Frame commit; stage(commit);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::OK); clearHistory();
    ScienceBytes data; U8 length;
    for (unsigned failure = 0; failure < 8; ++failure) {
        if (!invoke_to_radioNext(0, data, length)) break;
        invoke_to_radioSent(0, false); tick();
    }
    ASSERT_FALSE(invoke_to_radioNext(0, data, length));
    ASSERT_EVENTS_ScienceFailed_SIZE(1);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::OK);  // Failure retains committed product.
}
void ScienceCoordinatorTester::failedStageAbortsAndInvalidates() {
    TwinScience::Frame frame; capture(frame);
    frame.set_operation(TwinScience::Operation::PRODUCT_COMMITTED);
    frame.set_productId(42); frame.set_totalBytes(9); frame.set_crc(0x29B1);
    invoke_to_messageIn(0, frame); tick();
    frame = fromPortHistory_messageOut->at(0).frame;
    clearHistory(); frame.set_operation(TwinScience::Operation::STAGE_BEGIN);
    invoke_to_messageIn(0, frame); tick();
    clearHistory(); frame.set_operation(TwinScience::Operation::STAGE_CHUNK);
    frame.set_offset(1); frame.set_length(9);  // A gap must abort the partial cache.
    invoke_to_messageIn(0, frame); tick();
    ASSERT_EVENTS_ScienceFailed_SIZE(1); ASSERT_from_messageOut_SIZE(1);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::ABORT);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::EXECUTION_ERROR);
    clearHistory(); frame.set_operation(TwinScience::Operation::STAGE_COMMIT);
    invoke_to_messageIn(0, frame); tick(); ASSERT_from_messageOut_SIZE(0);
}
void ScienceCoordinatorTester::newScheduleRetiresOldRepair() {
    TwinScience::Frame commit; stage(commit);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::OK);
    ScienceBytes packet; U8 length;
    ASSERT_TRUE(invoke_to_radioNext(0, packet, length));
    const U8 transfer = packet[3]; invoke_to_radioSent(0, true); drainRadio();
    invoke_to_payloadStateIn(0, PayloadState::READY);
    ASSERT_EQ(invoke_to_scheduleIn(0, 2), Fw::CmdResponse::OK);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::BUSY);
    clearHistory();
    ScienceBytes retry;
    retry[0] = 0x4E; retry[1] = 0x32; retry[2] = 4; retry[3] = transfer;
    retry[4] = 0; retry[5] = 0; retry[6] = 1; retry[7] = 1;
    invoke_to_radioRetry(0, retry, 8);
    ASSERT_EVENTS_ScienceProgress_SIZE(0);
    ASSERT_FALSE(invoke_to_radioNext(0, packet, length));
}
void ScienceCoordinatorTester::explicitRestageAfterReset() {
    invoke_to_payloadStateIn(0, PayloadState::READY);
    sendCmd_RESTAGE_PRODUCT(0, 1, 42); tick(); ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_from_messageOut_SIZE(1);
    auto frame = fromPortHistory_messageOut->at(0).frame;
    ASSERT_EQ(frame.get_operation(), TwinScience::Operation::STAGE_REQUEST);
    ASSERT_EQ(frame.get_totalBytes(), 0U);  // FC lost volatile descriptor on reset.
    frame.set_totalBytes(9); frame.set_crc(0x29B1); frame.set_operation(TwinScience::Operation::STAGE_BEGIN);
    clearHistory(); invoke_to_messageIn(0, frame); tick(); ASSERT_EVENTS_ScienceFailed_SIZE(0);
    TwinScience::Data data; const char* bytes = "123456789";
    for (unsigned i = 0; i < 9; ++i) data[i] = bytes[i];
    frame.set_operation(TwinScience::Operation::STAGE_CHUNK); frame.set_length(9); frame.set_data(data);
    invoke_to_messageIn(0, frame); tick();
    frame.set_operation(TwinScience::Operation::STAGE_COMMIT); frame.set_offset(9); frame.set_length(0);
    invoke_to_messageIn(0, frame); tick();
    ASSERT_EVENTS_ScienceFailed_SIZE(0);
    ASSERT_EQ(invoke_to_downlinkIn(0), Fw::CmdResponse::OK); drainRadio();
}
}
