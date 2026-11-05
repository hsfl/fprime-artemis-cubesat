#include "PayloadManagerTester.hpp"

#include <cstdio>
#include <unistd.h>

namespace Components {

namespace {
constexpr const char* PRODUCT_PATH = "/tmp/payload_manager_ut.bin";
constexpr U8 PRODUCT[] = {1U, 2U, 3U, 4U};
constexpr U32 CAPTURE_SECONDS = 5U;

U32 productCrc() {
    U16 crc = 0xFFFFU;
    for (const U8 value : PRODUCT) {
        crc ^= static_cast<U16>(value) << 8U;
        for (U8 bit = 0; bit < 8U; ++bit) {
            crc = ((crc & 0x8000U) != 0U) ? static_cast<U16>((crc << 1U) ^ 0x1021U) : static_cast<U16>(crc << 1U);
        }
    }
    return crc;
}

bool writeProduct() {
    std::FILE* file = std::fopen(PRODUCT_PATH, "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t written = std::fwrite(PRODUCT, 1U, sizeof(PRODUCT), file);
    return (std::fclose(file) == 0) && (written == sizeof(PRODUCT));
}
}  // namespace

PayloadManagerTester::PayloadManagerTester()
    : PayloadManagerGTestBase("PayloadManagerTester", MAX_HISTORY_SIZE),
      component("PayloadManager") {
    this->initComponents();
    this->connectPorts();
}

PayloadManagerTester::~PayloadManagerTester() {
    this->component.deinit();
    (void)::unlink(PRODUCT_PATH);
}

void PayloadManagerTester::applyControl(Fw::Enabled control, bool activeOperation) {
    const FwSizeType before = this->fromPortHistory_captureControlReportOut->size();
    const FwSizeType tlmBefore = this->tlmHistory_CaptureInhibited->size();
    this->invoke_to_captureControlIn(0, control);
    this->component.doDispatch();
    ASSERT_from_captureControlReportOut_SIZE(before + 1);
    ASSERT_from_captureControlReportOut(before, control, activeOperation);
    ASSERT_TLM_CaptureInhibited_SIZE(tlmBefore + 1);
    ASSERT_TLM_CaptureInhibited(tlmBefore, control == Fw::Enabled::DISABLED);
}

// Both ground commands that start a capture hit the one gate. Nothing reaches the driver.
void PayloadManagerTester::testDirectCaptureCommandsRefusedWhileInhibited() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->applyControl(Fw::Enabled::DISABLED, false));

    this->sendCmd_SCIENCE_CAPTURE(0, 1, CAPTURE_SECONDS);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(0, PayloadManagerComponentBase::OPCODE_SCIENCE_CAPTURE, 1, Fw::CmdResponse::EXECUTION_ERROR);

    this->sendCmd_START_PAYLOAD_COLLECTION(0, 2, 7U);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(1, PayloadManagerComponentBase::OPCODE_START_PAYLOAD_COLLECTION, 2,
                        Fw::CmdResponse::EXECUTION_ERROR);

    ASSERT_from_driverRequestOut_SIZE(0);
    ASSERT_EVENTS_PayloadOperationStatus_SIZE(2);
    ASSERT_EQ(this->eventHistory_PayloadOperationStatus->at(0).state, Components::PayloadOperationState::INHIBITED);
}

// A ScienceApp request goes through the same gate and comes back as a notice with no generation.
void PayloadManagerTester::testScienceRequestRefusedWhileInhibited() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->applyControl(Fw::Enabled::DISABLED, false));

    this->invoke_to_requestIn(0, 9U, CAPTURE_SECONDS);
    this->component.doDispatch();

    ASSERT_from_driverRequestOut_SIZE(0);
    ASSERT_from_operationNoticeOut_SIZE(1);
    ASSERT_EQ(this->fromPortHistory_operationNoticeOut->at(0).requestId, 9U);
    ASSERT_EQ(this->fromPortHistory_operationNoticeOut->at(0).generation, 0U);
    ASSERT_EQ(this->fromPortHistory_operationNoticeOut->at(0).state, Components::PayloadOperationState::INHIBITED);
    // ScienceApp only closes a verified capture on a notice marked as its own.
    ASSERT_TRUE(this->fromPortHistory_operationNoticeOut->at(0).scienceRequested);
}

void PayloadManagerTester::testPassthroughRefusedWhileInhibited() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->applyControl(Fw::Enabled::DISABLED, false));

    this->sendCmd_SYNTHETIC_PASSTHROUGH(0, 1, 5U, Fw::CmdStringArg("HELLO"));
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE(0, PayloadManagerComponentBase::OPCODE_SYNTHETIC_PASSTHROUGH, 1,
                        Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_from_driverPassthroughOut_SIZE(0);
}

void PayloadManagerTester::testCapturesResumeWhenEnabled() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->applyControl(Fw::Enabled::DISABLED, false));
    ASSERT_NO_FATAL_FAILURE(this->applyControl(Fw::Enabled::ENABLED, false));

    this->invoke_to_requestIn(0, 9U, CAPTURE_SECONDS);
    this->component.doDispatch();

    ASSERT_from_driverRequestOut_SIZE(1);
    ASSERT_EQ(this->fromPortHistory_operationNoticeOut->at(0).state, Components::PayloadOperationState::ACCEPTED);
}

// Dennis, 2026-11-04: a capture running when SAFE starts finishes. Its report says one is running.
void PayloadManagerTester::testRunningCaptureFinishesUnderInhibit() {
    this->clearHistory();
    ASSERT_TRUE(writeProduct());
    this->invoke_to_requestIn(0, 9U, CAPTURE_SECONDS);
    this->component.doDispatch();
    ASSERT_from_driverRequestOut_SIZE(1);
    const U32 generation = this->fromPortHistory_driverRequestOut->at(0).generation;

    ASSERT_NO_FATAL_FAILURE(this->applyControl(Fw::Enabled::DISABLED, true));

    const Fw::String empty("");
    const Fw::String path(PRODUCT_PATH);
    this->invoke_to_driverOperationResultIn(0, 9U, generation, Components::PayloadOperationKind::CAPTURE,
                                            Components::PayloadOperationState::COMPLETED, empty, 23U,
                                            sizeof(PRODUCT), Components::ScienceProductSource::NEUTRON_SIM, path,
                                            productCrc(), 0U);
    this->component.doDispatch();
    ASSERT_from_operationNoticeOut_SIZE(2);
    ASSERT_EQ(this->fromPortHistory_operationNoticeOut->at(1).state, Components::PayloadOperationState::COMPLETED);
}

// Queue order decides: a request behind the inhibit is refused, whichever caller sent it.
void PayloadManagerTester::testRequestQueuedAfterInhibitIsRefused() {
    this->clearHistory();
    this->invoke_to_captureControlIn(0, Fw::Enabled::DISABLED);
    this->invoke_to_requestIn(0, 9U, CAPTURE_SECONDS);
    this->component.doDispatch();
    this->component.doDispatch();

    ASSERT_from_driverRequestOut_SIZE(0);
    ASSERT_from_operationNoticeOut_SIZE(1);
    ASSERT_EQ(this->fromPortHistory_operationNoticeOut->at(0).state, Components::PayloadOperationState::INHIBITED);
}

// A build that never connects the control keeps today's behavior, which is what C3M's topologies need.
void PayloadManagerTester::testCapturesStartBeforeAnyControlArrives() {
    this->clearHistory();
    this->invoke_to_requestIn(0, 9U, CAPTURE_SECONDS);
    this->component.doDispatch();
    ASSERT_from_driverRequestOut_SIZE(1);
}

}  // namespace Components
