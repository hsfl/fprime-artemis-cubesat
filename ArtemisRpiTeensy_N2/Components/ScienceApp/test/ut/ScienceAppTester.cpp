#include "ScienceAppTester.hpp"

#include <cstdio>
#include <unistd.h>

namespace Components {

namespace {
constexpr const char* AUTO_DOWNLINK_TEST_PATH = "/tmp/science_app_auto_downlink_ut.bin";

U16 scienceProductCrc(const U8* data, FwSizeType size) {
    U16 crc = 0xFFFFU;
    for (FwSizeType index = 0; index < size; ++index) {
        crc ^= static_cast<U16>(data[index]) << 8U;
        for (U8 bit = 0; bit < 8U; ++bit) {
            crc = ((crc & 0x8000U) != 0U)
                ? static_cast<U16>((crc << 1U) ^ 0x1021U)
                : static_cast<U16>(crc << 1U);
        }
    }
    return crc;
}

bool writeScienceProduct(const U8* data, FwSizeType size) {
    std::FILE* file = std::fopen(AUTO_DOWNLINK_TEST_PATH, "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t written = std::fwrite(data, 1U, static_cast<std::size_t>(size), file);
    return (std::fclose(file) == 0) && (written == static_cast<std::size_t>(size));
}
}  // namespace

ScienceAppTester::ScienceAppTester()
    : ScienceAppGTestBase("ScienceAppTester", MAX_HISTORY_SIZE),
      component("ScienceApp") {
    this->initComponents();
    this->connectPorts();
}

ScienceAppTester::~ScienceAppTester() {
    this->component.deinit();
    (void)::unlink(AUTO_DOWNLINK_TEST_PATH);
}

void ScienceAppTester::initComponents() {
    this->init();
    this->component.init(TEST_INSTANCE_QUEUE_DEPTH, TEST_INSTANCE_ID);
}

void ScienceAppTester::connectPorts() {
    this->connect_to_cmdIn(0, this->component.get_cmdIn_InputPort(0));
    this->connect_to_cancelRequestIn(0, this->component.get_cancelRequestIn_InputPort(0));
    this->connect_to_operationNoticeIn(0, this->component.get_operationNoticeIn_InputPort(0));
    this->connect_to_pingIn(0, this->component.get_pingIn_InputPort(0));
    this->connect_to_requestIn(0, this->component.get_requestIn_InputPort(0));
    this->connect_to_run(0, this->component.get_run_InputPort(0));

    this->component.set_cmdRegOut_OutputPort(0, this->get_from_cmdRegOut(0));
    this->component.set_cmdResponseOut_OutputPort(0, this->get_from_cmdResponseOut(0));
    this->component.set_logOut_OutputPort(0, this->get_from_logOut(0));
#if FW_ENABLE_TEXT_LOGGING == 1
    this->component.set_logTextOut_OutputPort(0, this->get_from_logTextOut(0));
#endif
    this->component.set_prmGetOut_OutputPort(0, this->get_from_prmGetOut(0));
    this->component.set_prmSetOut_OutputPort(0, this->get_from_prmSetOut(0));
    this->component.set_timeCaller_OutputPort(0, this->get_from_timeCaller(0));
    this->component.set_tlmOut_OutputPort(0, this->get_from_tlmOut(0));

    this->component.set_deliveryRequestOut_OutputPort(0, this->get_from_deliveryRequestOut(0));
    this->component.set_missionModeOut_OutputPort(0, this->get_from_missionModeOut(0));
    this->component.set_payloadRequestOut_OutputPort(0, this->get_from_payloadRequestOut(0));
    this->component.set_pingOut_OutputPort(0, this->get_from_pingOut(0));
    this->component.set_scienceProductOut_OutputPort(0, this->get_from_scienceProductOut(0));
}

void ScienceAppTester::connectConfiguredCapturePorts() {
    this->connect_to_durationConfigResultIn(0, this->component.get_durationConfigResultIn_InputPort(0));
    this->component.set_durationConfigOut_OutputPort(0, this->get_from_durationConfigOut(0));
    this->component.set_configuredCaptureOut_OutputPort(0, this->get_from_configuredCaptureOut(0));
}

void ScienceAppTester::testDefaultDurationIsThirty() {
    this->clearHistory();

    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 1, 30);
    ASSERT_from_missionModeOut_SIZE(0);

    Fw::String sourcePath("");
    this->invoke_to_operationNoticeIn(0, 1, 1, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::ACCEPTED, 0, 0,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0, true);
    this->component.doDispatch();
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 30);

    this->invoke_to_operationNoticeIn(0, 1, 1, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::FAILED, 0, 0,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0, true);
    this->component.doDispatch();
    ASSERT_from_missionModeOut_SIZE(2);
    ASSERT_from_missionModeOut(1, Components::MissionMode::BASE, 0);
    ASSERT_TLM_CaptureDurationSeconds(0, 30);
}

void ScienceAppTester::testScheduledCountdownFiresExactlyOnce() {
    this->clearHistory();

    this->invoke_to_requestIn(0, 2);
    this->component.doDispatch();

    ASSERT_EVENTS_CollectionTriggered_SIZE(1);
    ASSERT_EVENTS_CollectionTriggered(0, 2);
    ASSERT_TLM_PendingDelaySeconds(0, 2);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_TLM_PendingDelaySeconds(1, 1);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 1, 30);
    ASSERT_from_missionModeOut_SIZE(0);
    Fw::String sourcePath("");
    this->invoke_to_operationNoticeIn(0, 1, 1, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::ACCEPTED, 0, 0,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0, true);
    this->component.doDispatch();
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 30);
    ASSERT_TLM_PendingDelaySeconds(2, 0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_missionModeOut_SIZE(1);
}

void ScienceAppTester::testRejectsZeroDelayAndBadDurations() {
    this->clearHistory();

    this->invoke_to_requestIn(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(1);
    ASSERT_EVENTS_ScienceCommandRejected(0, 1, 0);
    ASSERT_TLM_PendingDelaySeconds_SIZE(0);

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 0,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(2);
    ASSERT_EVENTS_ScienceCommandRejected(1, 2, 0);

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 1, 121);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 1,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(3);
    ASSERT_EVENTS_ScienceCommandRejected(2, 2, 121);

#ifndef NEUTRON2_CONFIGURED_CAPTURE
    this->sendCmd_SCIENCE_CAPTURE(0, 2, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(3);
    ASSERT_CMD_RESPONSE(2, ScienceAppComponentBase::OPCODE_SCIENCE_CAPTURE, 2,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(4);
    ASSERT_EVENTS_ScienceCommandRejected(3, 2, 0);
#endif
    ASSERT_from_payloadRequestOut_SIZE(0);

    this->clearHistory();
    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 3, 120);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 3,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_CaptureDurationConfigured_SIZE(1);
    ASSERT_EVENTS_CaptureDurationConfigured(0, 120);
}

void ScienceAppTester::testCancelClearsCountdown() {
    this->clearHistory();

    this->invoke_to_requestIn(0, 2);
    this->component.doDispatch();
    this->invoke_to_cancelRequestIn(0, 0);
    this->component.doDispatch();

    ASSERT_EVENTS_CollectionCancelled_SIZE(1);
    ASSERT_EVENTS_CollectionCancelled(0, 2);
    ASSERT_TLM_PendingDelaySeconds(1, 0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
}

void ScienceAppTester::testQueuedCancelAtFinalTickBoundaryPreventsCollection() {
    this->clearHistory();

    this->invoke_to_requestIn(0, 2);
    this->component.doDispatch();

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_TLM_PendingDelaySeconds(1, 1);
    ASSERT_from_payloadRequestOut_SIZE(0);

    this->invoke_to_cancelRequestIn(0, 0);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_CollectionCancelled_SIZE(1);
    ASSERT_EVENTS_CollectionCancelled(0, 1);
    ASSERT_TLM_PendingDelaySeconds(2, 0);

    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
}

#ifndef NEUTRON2_CONFIGURED_CAPTURE
void ScienceAppTester::testScienceCaptureDoesNotPersistDuration() {
    this->clearHistory();

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 0, 45);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_TLM_CaptureDurationSeconds(0, 45);

    this->clearHistory();
    this->sendCmd_SCIENCE_CAPTURE(0, 1, 5);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_SCIENCE_CAPTURE, 1,
                        Fw::CmdResponse::OK);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 1, 5);
    ASSERT_from_missionModeOut_SIZE(0);
    ASSERT_TLM_CaptureDurationSeconds_SIZE(0);
    ASSERT_EVENTS_CaptureDurationConfigured_SIZE(0);

    this->clearHistory();
    this->sendCmd_START_COLLECTION(0, 2);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 2, 45);
    ASSERT_from_missionModeOut_SIZE(0);
    ASSERT_TLM_CaptureDurationSeconds_SIZE(0);
}
#endif

void ScienceAppTester::testConfiguredCaptureWaitsForMatchingDurationResult() {
    this->connectConfiguredCapturePorts();
    this->component.preamble();
    ASSERT_TLM_CaptureDurationSeconds(0, 0U);

    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_START_COLLECTION, 0,
                        Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_from_configuredCaptureOut_SIZE(0);
    ASSERT_from_payloadRequestOut_SIZE(0);
    this->clearHistory();

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 1, 60U);
    this->component.doDispatch();
    ASSERT_from_durationConfigOut_SIZE(1);
    ASSERT_from_durationConfigOut(0, 1U, 60U);
    ASSERT_CMD_RESPONSE_SIZE(0);

    this->invoke_to_durationConfigResultIn(0, 2U, PayloadOperationState::COMPLETED);
    this->component.doDispatch();
    this->invoke_to_durationConfigResultIn(0, 1U, PayloadOperationState::ACCEPTED);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(0);

    this->sendCmd_START_COLLECTION(0, 2);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_START_COLLECTION, 2,
                        Fw::CmdResponse::EXECUTION_ERROR);
#ifdef NEUTRON2_CONFIGURED_CAPTURE
    this->sendCmd_SCIENCE_CAPTURE(0, 3);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, ScienceAppComponentBase::OPCODE_SCIENCE_CAPTURE, 3,
                        Fw::CmdResponse::EXECUTION_ERROR);
#endif
    ASSERT_from_configuredCaptureOut_SIZE(0);
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_EVENTS_CaptureDurationConfigured_SIZE(0);
    ASSERT_TLM_CaptureDurationSeconds_SIZE(0);

    this->clearHistory();
    this->invoke_to_durationConfigResultIn(0, 1U, PayloadOperationState::COMPLETED);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 1,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_CaptureDurationConfigured_SIZE(1);
    ASSERT_EVENTS_CaptureDurationConfigured(0, 60U);
    ASSERT_TLM_CaptureDurationSeconds_SIZE(1);
    ASSERT_TLM_CaptureDurationSeconds(0, 60U);

    this->sendCmd_START_COLLECTION(0, 4);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, ScienceAppComponentBase::OPCODE_START_COLLECTION, 4, Fw::CmdResponse::OK);
    ASSERT_from_configuredCaptureOut_SIZE(1);
    ASSERT_from_configuredCaptureOut(0, 2U);
#ifdef NEUTRON2_CONFIGURED_CAPTURE
    this->sendCmd_SCIENCE_CAPTURE(0, 5);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(3);
    ASSERT_CMD_RESPONSE(2, ScienceAppComponentBase::OPCODE_SCIENCE_CAPTURE, 5, Fw::CmdResponse::OK);
    ASSERT_from_configuredCaptureOut_SIZE(2);
    ASSERT_from_configuredCaptureOut(1, 3U);
#endif
    ASSERT_from_payloadRequestOut_SIZE(0);
}

void ScienceAppTester::testConfiguredDurationFailureAllowsRetry() {
    this->connectConfiguredCapturePorts();
    this->component.preamble();
    this->clearHistory();

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 0, 60U);
    this->component.doDispatch();
    this->invoke_to_durationConfigResultIn(0, 1U, PayloadOperationState::COMPLETED);
    this->component.doDispatch();
    ASSERT_TLM_CaptureDurationSeconds(0, 60U);
    this->clearHistory();

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 1, 90U);
    this->component.doDispatch();
    this->invoke_to_durationConfigResultIn(0, 2U, PayloadOperationState::FAILED);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 1,
                        Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_ScienceOperationRejected_SIZE(1);
    ASSERT_EVENTS_ScienceOperationRejected(0, 2U, PayloadOperationState::FAILED);
    ASSERT_EVENTS_CaptureDurationConfigured_SIZE(0);
    ASSERT_TLM_CaptureDurationSeconds_SIZE(1);
    ASSERT_TLM_CaptureDurationSeconds(0, 0U);

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 2, 45U);
    this->component.doDispatch();
    ASSERT_from_durationConfigOut_SIZE(2);
    ASSERT_from_durationConfigOut(1, 3U, 45U);

    this->invoke_to_durationConfigResultIn(0, 2U, PayloadOperationState::COMPLETED);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);

    this->invoke_to_durationConfigResultIn(0, 3U, PayloadOperationState::COMPLETED);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 2,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_CaptureDurationConfigured_SIZE(1);
    ASSERT_EVENTS_CaptureDurationConfigured(0, 45U);
}

void ScienceAppTester::testConfiguredDurationTimesOut() {
    this->connectConfiguredCapturePorts();
    this->component.preamble();
    this->clearHistory();

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 0, 60U);
    this->component.doDispatch();
    this->invoke_to_durationConfigResultIn(0, 1U, PayloadOperationState::COMPLETED);
    this->component.doDispatch();
    ASSERT_TLM_CaptureDurationSeconds(0, 60U);
    this->clearHistory();

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 1, 45U);
    this->component.doDispatch();
    for (U32 tick = 0; tick < 29U; ++tick) {
        this->invoke_to_run(0, 0);
        this->component.doDispatch();
    }
    ASSERT_CMD_RESPONSE_SIZE(0);

    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 1,
                        Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_ScienceOperationRejected_SIZE(1);
    ASSERT_EVENTS_ScienceOperationRejected(0, 2U, PayloadOperationState::TIMED_OUT);

    this->invoke_to_durationConfigResultIn(0, 2U, PayloadOperationState::COMPLETED);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_CaptureDurationConfigured_SIZE(0);
    ASSERT_TLM_CaptureDurationSeconds_SIZE(1);
    ASSERT_TLM_CaptureDurationSeconds(0, 0U);

    this->sendCmd_START_COLLECTION(0, 2);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, ScienceAppComponentBase::OPCODE_START_COLLECTION, 2,
                        Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_from_configuredCaptureOut_SIZE(0);
    ASSERT_from_payloadRequestOut_SIZE(0);
}

void ScienceAppTester::testScheduledCaptureReturnsToBaseIfDurationMissingOrPending() {
    this->connectConfiguredCapturePorts();
    this->component.preamble();
    this->clearHistory();

    this->invoke_to_requestIn(0, 1U);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, MissionMode::BASE, 0U);
    ASSERT_from_configuredCaptureOut_SIZE(0);
    ASSERT_from_payloadRequestOut_SIZE(0);

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 0, 60U);
    this->component.doDispatch();
    this->invoke_to_durationConfigResultIn(0, 1U, PayloadOperationState::COMPLETED);
    this->component.doDispatch();
    ASSERT_TLM_CaptureDurationSeconds(0, 60U);
    this->clearHistory();

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 1, 45U);
    this->component.doDispatch();
    this->invoke_to_requestIn(0, 1U);
    this->component.doDispatch();
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, MissionMode::BASE, 0U);
    ASSERT_from_configuredCaptureOut_SIZE(0);
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(0);
}

void ScienceAppTester::testParamSeedsDurationAtInit() {
    this->paramSet_CAPTURE_DURATION_SECONDS(45, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->component.preamble();

    ASSERT_EVENTS_ScienceCommandRejected_SIZE(0);
    ASSERT_TLM_CaptureDurationSeconds(0, 45);

    this->clearHistory();
    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_START_COLLECTION, 0,
                        Fw::CmdResponse::OK);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 1, 45);
    ASSERT_from_missionModeOut_SIZE(0);
}

void ScienceAppTester::testInvalidParamFallsBackToThirty() {
    this->paramSet_CAPTURE_DURATION_SECONDS(121, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->component.preamble();

    ASSERT_EVENTS_ScienceCommandRejected_SIZE(1);
    ASSERT_EVENTS_ScienceCommandRejected(0, 2, 121);
    ASSERT_TLM_CaptureDurationSeconds(0, 30);

    this->clearHistory();
    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_START_COLLECTION, 0,
                        Fw::CmdResponse::OK);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 1, 30);
    ASSERT_from_missionModeOut_SIZE(0);
}

void ScienceAppTester::testCollectAndDownlinkWaitsForValidatedCompletion() {
    const U8 product[] = {0x4E, 0x32, 0x01, 0xFE};
    ASSERT_TRUE(writeScienceProduct(product, sizeof(product)));
    this->clearHistory();

    this->sendCmd_COLLECT_AND_DOWNLINK(0, 0, 4U);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_COLLECT_AND_DOWNLINK, 0, Fw::CmdResponse::OK);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 1, 4U);
    ASSERT_from_deliveryRequestOut_SIZE(0);

    Fw::String sourcePath(AUTO_DOWNLINK_TEST_PATH);
    // The direct manager request owns this ID first. Its accepted operation
    // must still publish the legacy C product, without consuming the pending
    // ScienceApp request's automatic-delivery intent.
    this->invoke_to_operationNoticeIn(0, 1, 41, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::ACCEPTED, 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0U, false);
    this->component.doDispatch();
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 30U);
    ASSERT_from_deliveryRequestOut_SIZE(0);

    // ScienceApp's same-ID request is rejected BUSY. It must clear only its
    // unbound automatic intent and leave the foreign operation in control.
    this->invoke_to_operationNoticeIn(0, 1, 0, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::BUSY, 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0U, true);
    this->component.doDispatch();

    this->invoke_to_operationNoticeIn(0, 1, 41, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::COMPLETED, 23U, sizeof(product),
                                      Components::ScienceProductSource::NEUTRON_SIM, sourcePath,
                                      scienceProductCrc(product, sizeof(product)), false);
    this->component.doDispatch();
    ASSERT_from_scienceProductOut_SIZE(1);
    ASSERT_from_deliveryRequestOut_SIZE(0);

    // A later ScienceApp-owned operation still binds and completes its own
    // automatic-delivery intent.
    this->sendCmd_COLLECT_AND_DOWNLINK(0, 1, 5U);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(2);
    ASSERT_from_payloadRequestOut(1, 2, 5U);
    this->invoke_to_operationNoticeIn(0, 2, 42, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::ACCEPTED, 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0U, true);
    this->component.doDispatch();
    this->invoke_to_operationNoticeIn(0, 2, 42, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::COMPLETED, 24U, sizeof(product),
                                      Components::ScienceProductSource::NEUTRON_SIM, sourcePath,
                                      scienceProductCrc(product, sizeof(product)), true);
    this->component.doDispatch();
    ASSERT_from_scienceProductOut_SIZE(2);
    ASSERT_from_deliveryRequestOut_SIZE(1);
    ASSERT_from_deliveryRequestOut(0, 24U);
    ASSERT_TLM_CollectionCount_SIZE(3);
    ASSERT_TLM_CollectionCount(2, 2U);
}

void ScienceAppTester::testCollectAndDownlinkClearsIntentOnFailure() {
    this->clearHistory();
    this->sendCmd_COLLECT_AND_DOWNLINK(0, 0, 4U);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(1);

    Fw::String sourcePath("");
    this->invoke_to_operationNoticeIn(0, 1, 52, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::ACCEPTED, 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0U, true);
    this->component.doDispatch();
    this->invoke_to_operationNoticeIn(0, 1, 52, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::FAILED, 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0U, true);
    this->component.doDispatch();
    ASSERT_from_deliveryRequestOut_SIZE(0);

    // A late terminal callback for the failed operation must not revive its intent.
    const U8 product[] = {0x4E, 0x32};
    ASSERT_TRUE(writeScienceProduct(product, sizeof(product)));
    Fw::String validPath(AUTO_DOWNLINK_TEST_PATH);
    this->invoke_to_operationNoticeIn(0, 1, 52, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::COMPLETED, 23U, sizeof(product),
                                      Components::ScienceProductSource::NEUTRON_SIM, validPath,
                                      scienceProductCrc(product, sizeof(product)), true);
    this->component.doDispatch();
    ASSERT_from_deliveryRequestOut_SIZE(0);
    ASSERT_from_scienceProductOut_SIZE(0);
}

void ScienceAppTester::testCdrRejectsAutomaticDownlinkBeforeCaptureOrTokenAllocation() {
#ifdef NEUTRON2_CDR_TCP
    constexpr U32 REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED = 3U;
    this->clearHistory();

    this->sendCmd_COLLECT_AND_DOWNLINK(0, 0, 5U);
    this->component.doDispatch();
    this->sendCmd_VERIFIED_COLLECT_AND_DOWNLINK(0, 1, 42U, 5U);
    this->component.doDispatch();
    // Reusing the token remains an unsupported-workflow rejection, proving the
    // earlier request did not consume a token-table entry.
    this->sendCmd_VERIFIED_COLLECT_AND_DOWNLINK(0, 2, 42U, 5U);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(3);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_COLLECT_AND_DOWNLINK, 0,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_CMD_RESPONSE(1, ScienceAppComponentBase::OPCODE_VERIFIED_COLLECT_AND_DOWNLINK, 1,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_CMD_RESPONSE(2, ScienceAppComponentBase::OPCODE_VERIFIED_COLLECT_AND_DOWNLINK, 2,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(3);
    ASSERT_EVENTS_ScienceCommandRejected(0, REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED,
                                         ScienceAppComponentBase::OPCODE_COLLECT_AND_DOWNLINK);
    ASSERT_EVENTS_ScienceCommandRejected(1, REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED,
                                         ScienceAppComponentBase::OPCODE_VERIFIED_COLLECT_AND_DOWNLINK);
    ASSERT_EVENTS_ScienceCommandRejected(2, REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED,
                                         ScienceAppComponentBase::OPCODE_VERIFIED_COLLECT_AND_DOWNLINK);
    ASSERT_EVENTS_VerifiedCaptureStatus_SIZE(0);
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_from_deliveryRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
#endif
}

}  // namespace Components
