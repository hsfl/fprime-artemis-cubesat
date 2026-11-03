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

    this->sendCmd_SCIENCE_CAPTURE(0, 2, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(3);
    ASSERT_CMD_RESPONSE(2, ScienceAppComponentBase::OPCODE_SCIENCE_CAPTURE, 2,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceCommandRejected_SIZE(4);
    ASSERT_EVENTS_ScienceCommandRejected(3, 2, 0);
    ASSERT_from_payloadRequestOut_SIZE(0);

    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 3, 120);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(4);
    ASSERT_CMD_RESPONSE(3, ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 3,
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

// ----------------------------------------------------------------------
// SAFE inhibits science (SDD v4.0 Table 5)
// ----------------------------------------------------------------------

void ScienceAppTester::setMissionMode(Components::MissionMode mode) {
    this->invoke_to_missionModeIn(0, mode, 0U);
    this->component.doDispatch();
}

void ScienceAppTester::notifyCapture(U32 requestId, U32 generation, Components::PayloadOperationState state,
                                     U32 productId, U32 productBytes, U32 sourceCrc, bool scienceRequested) {
    const bool completed = (state == Components::PayloadOperationState::COMPLETED);
    Fw::String sourcePath(completed ? AUTO_DOWNLINK_TEST_PATH : "");
    this->invoke_to_operationNoticeIn(0, requestId, generation, Components::PayloadOperationKind::CAPTURE, state,
                                      productId, productBytes,
                                      completed ? Components::ScienceProductSource::NEUTRON_SIM
                                                : Components::ScienceProductSource::UNKNOWN,
                                      sourcePath, sourceCrc, scienceRequested);
    this->component.doDispatch();
}

void ScienceAppTester::testSafeRefusesEveryCaptureCommand() {
    this->clearHistory();
    this->setMissionMode(Components::MissionMode::SAFE);
    ASSERT_EVENTS_ScienceInhibitChanged_SIZE(1);
    ASSERT_EVENTS_ScienceInhibitChanged(0, 1U, Components::MissionMode::SAFE);

    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();
    this->sendCmd_SCIENCE_CAPTURE(0, 1, 5U);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_START_COLLECTION, 0, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_CMD_RESPONSE(1, ScienceAppComponentBase::OPCODE_SCIENCE_CAPTURE, 1, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceInhibited_SIZE(2);
    ASSERT_EVENTS_ScienceInhibited(0, Components::ScienceInhibitSource::START_COLLECTION);
    ASSERT_EVENTS_ScienceInhibited(1, Components::ScienceInhibitSource::SCIENCE_CAPTURE);
#ifndef NEUTRON2_CDR_TCP
    // The automatic-downlink commands run in local builds. The CDR build refuses them for another reason.
    this->sendCmd_COLLECT_AND_DOWNLINK(0, 2, 5U);
    this->component.doDispatch();
    this->sendCmd_VERIFIED_COLLECT_AND_DOWNLINK(0, 3, 77U, 5U);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(4);
    ASSERT_CMD_RESPONSE(2, ScienceAppComponentBase::OPCODE_COLLECT_AND_DOWNLINK, 2, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_CMD_RESPONSE(3, ScienceAppComponentBase::OPCODE_VERIFIED_COLLECT_AND_DOWNLINK, 3,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_ScienceInhibited_SIZE(4);
    ASSERT_EVENTS_ScienceInhibited(2, Components::ScienceInhibitSource::COLLECT_AND_DOWNLINK);
    ASSERT_EVENTS_ScienceInhibited(3, Components::ScienceInhibitSource::VERIFIED_COLLECT_AND_DOWNLINK);
    // The token gets a terminal answer (reason 7) without using a table entry.
    ASSERT_EVENTS_VerifiedCaptureStatus_SIZE(1);
    ASSERT_EVENTS_VerifiedCaptureStatus(0, 77U, 0U, 0U, Components::PayloadOperationState::INVALID, 0U, 0U, 0U, 7U);
#endif
    // Nothing reached PayloadManager, StorageManager or MissionApp.
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_from_scienceProductOut_SIZE(0);
    ASSERT_from_deliveryRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);

    // Setting the duration starts nothing, so it still works in SAFE.
    this->clearHistory();
    this->sendCmd_CONFIGURE_CAPTURE_DURATION(0, 4, 10U);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION, 4, Fw::CmdResponse::OK);
    ASSERT_EVENTS_ScienceInhibited_SIZE(0);
}

void ScienceAppTester::testSafeRefusesScheduledRequestAndCountdown() {
    this->clearHistory();
    this->setMissionMode(Components::MissionMode::SAFE);
    this->invoke_to_requestIn(0, 2U);
    this->component.doDispatch();
    ASSERT_EVENTS_ScienceInhibited_SIZE(1);
    ASSERT_EVENTS_ScienceInhibited(0, Components::ScienceInhibitSource::SCHEDULED_REQUEST);
    ASSERT_EVENTS_CollectionTriggered_SIZE(0);
    ASSERT_TLM_PendingDelaySeconds_SIZE(0);

    // A countdown started in BASE that reaches zero in SAFE (here no cancel arrives, as if it were
    // lost) still sends nothing to PayloadManager.
    this->setMissionMode(Components::MissionMode::BASE);
    this->invoke_to_requestIn(0, 2U);
    this->component.doDispatch();
    ASSERT_EVENTS_CollectionTriggered_SIZE(1);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    this->setMissionMode(Components::MissionMode::SAFE);
    this->invoke_to_run(0, 0);
    this->component.doDispatch();
    ASSERT_EVENTS_ScienceInhibited_SIZE(2);
    ASSERT_EVENTS_ScienceInhibited(1, Components::ScienceInhibitSource::SCHEDULED_COUNTDOWN);
    ASSERT_from_payloadRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
}

void ScienceAppTester::testCapturesAllowedAgainAfterSafe() {
    this->clearHistory();
    this->setMissionMode(Components::MissionMode::SAFE);
    this->setMissionMode(Components::MissionMode::BASE);
    ASSERT_EVENTS_ScienceInhibitChanged_SIZE(2);
    ASSERT_EVENTS_ScienceInhibitChanged(1, 0U, Components::MissionMode::BASE);

    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_START_COLLECTION, 0, Fw::CmdResponse::OK);
    ASSERT_from_payloadRequestOut_SIZE(1);
    ASSERT_from_payloadRequestOut(0, 1U, 30U);
    ASSERT_EVENTS_ScienceInhibited_SIZE(0);

    // Mode updates outside SAFE change nothing here and log nothing.
    this->setMissionMode(Components::MissionMode::COLLECTING);
    this->setMissionMode(Components::MissionMode::BASE);
    ASSERT_EVENTS_ScienceInhibitChanged_SIZE(2);
}

void ScienceAppTester::testCaptureRunningAtSafeEntryIsWithheld() {
    const U8 product[] = {0x4E, 0x32, 0x05};
    ASSERT_TRUE(writeScienceProduct(product, sizeof(product)));
    const U32 bytes = static_cast<U32>(sizeof(product));
    this->clearHistory();

    // A capture starts in BASE and PayloadManager accepts it.
    this->sendCmd_SCIENCE_CAPTURE(0, 0, 5U);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut(0, 1U, 5U);
    this->notifyCapture(1U, 61U, Components::PayloadOperationState::ACCEPTED, 0U, 0U, 0U, true);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 30U);

    // SAFE begins while it runs. ScienceApp cannot stop the payload, and says so.
    this->setMissionMode(Components::MissionMode::SAFE);
    ASSERT_EVENTS_CaptureRunningInSafe_SIZE(1);
    ASSERT_EVENTS_CaptureRunningInSafe(0, 1U);

    // The capture ends in SAFE: its product is withheld. Nothing is stored, reported or sent.
    this->notifyCapture(1U, 61U, Components::PayloadOperationState::COMPLETED, 31U, bytes,
                        scienceProductCrc(product, sizeof(product)), true);
    ASSERT_EVENTS_CaptureWithheld_SIZE(1);
    ASSERT_EVENTS_CaptureWithheld(0, 1U, 31U, bytes);
    ASSERT_EVENTS_ScienceProductReady_SIZE(0);
    ASSERT_from_scienceProductOut_SIZE(0);
    ASSERT_from_deliveryRequestOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_TLM_CollectionCount_SIZE(1);
    ASSERT_TLM_CollectionCount(0, 0U);

    // That capture is closed: after SAFE a new capture goes out normally.
    this->setMissionMode(Components::MissionMode::BASE);
    this->sendCmd_SCIENCE_CAPTURE(0, 1, 5U);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(2);
    ASSERT_from_payloadRequestOut(1, 2U, 5U);
}

void ScienceAppTester::testManagerCaptureInSafeIsWithheld() {
    const U8 product[] = {0x4E, 0x32, 0x09, 0x10};
    ASSERT_TRUE(writeScienceProduct(product, sizeof(product)));
    const U32 bytes = static_cast<U32>(sizeof(product));
    this->clearHistory();
    this->setMissionMode(Components::MissionMode::SAFE);

    // PayloadManager's own SCIENCE_CAPTURE command, sent in SAFE. ScienceApp did not ask for it
    // (scienceRequested = false) and cannot refuse it. It reports no COLLECTING and flags the capture...
    this->notifyCapture(9U, 71U, Components::PayloadOperationState::ACCEPTED, 0U, 0U, 0U, false);
    ASSERT_EVENTS_CaptureRunningInSafe_SIZE(1);
    ASSERT_EVENTS_CaptureRunningInSafe(0, 9U);
    ASSERT_from_missionModeOut_SIZE(0);

    // ...and withholds its product.
    this->notifyCapture(9U, 71U, Components::PayloadOperationState::COMPLETED, 41U, bytes,
                        scienceProductCrc(product, sizeof(product)), false);
    ASSERT_EVENTS_CaptureWithheld_SIZE(1);
    ASSERT_EVENTS_CaptureWithheld(0, 9U, 41U, bytes);
    ASSERT_from_scienceProductOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(0);
}

void ScienceAppTester::testInterruptedCaptureStaysWithheldAfterSafeExit() {
    const U8 product[] = {0x4E, 0x32, 0x0A};
    ASSERT_TRUE(writeScienceProduct(product, sizeof(product)));
    const U32 bytes = static_cast<U32>(sizeof(product));
    const U32 crc = scienceProductCrc(product, sizeof(product));
    this->clearHistory();

    this->sendCmd_START_COLLECTION(0, 0);
    this->component.doDispatch();
    this->notifyCapture(1U, 81U, Components::PayloadOperationState::ACCEPTED, 0U, 0U, 0U, true);
    // SAFE comes and goes while the payload is still capturing.
    this->setMissionMode(Components::MissionMode::SAFE);
    this->setMissionMode(Components::MissionMode::BASE);

    // Part of this data was taken during SAFE, so the product is still withheld.
    this->notifyCapture(1U, 81U, Components::PayloadOperationState::COMPLETED, 51U, bytes, crc, true);
    ASSERT_EVENTS_CaptureRunningInSafe_SIZE(1);
    ASSERT_EVENTS_CaptureWithheld_SIZE(1);
    ASSERT_EVENTS_CaptureWithheld(0, 1U, 51U, bytes);
    ASSERT_from_scienceProductOut_SIZE(0);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 30U);

    // The next capture, wholly outside SAFE, is a normal product again.
    this->sendCmd_START_COLLECTION(0, 1);
    this->component.doDispatch();
    this->notifyCapture(2U, 82U, Components::PayloadOperationState::ACCEPTED, 0U, 0U, 0U, true);
    this->notifyCapture(2U, 82U, Components::PayloadOperationState::COMPLETED, 52U, bytes, crc, true);
    ASSERT_EVENTS_CaptureWithheld_SIZE(1);
    ASSERT_EVENTS_ScienceProductReady_SIZE(1);
    ASSERT_from_scienceProductOut_SIZE(1);
    ASSERT_from_missionModeOut_SIZE(3);
    ASSERT_from_missionModeOut(2, Components::MissionMode::SCIENCE_READY, bytes);
}

void ScienceAppTester::testVerifiedCaptureInterruptedBySafeEndsFailed() {
    const U8 product[] = {0x4E, 0x32, 0x0B, 0x0C};
    ASSERT_TRUE(writeScienceProduct(product, sizeof(product)));
    const U32 bytes = static_cast<U32>(sizeof(product));
    const U32 crc = scienceProductCrc(product, sizeof(product));
    this->clearHistory();

    this->sendCmd_VERIFIED_COLLECT_AND_DOWNLINK(0, 0, 55U, 5U);
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(0, ScienceAppComponentBase::OPCODE_VERIFIED_COLLECT_AND_DOWNLINK, 0, Fw::CmdResponse::OK);
    this->notifyCapture(1U, 91U, Components::PayloadOperationState::ACCEPTED, 0U, 0U, 0U, true);
    ASSERT_EVENTS_VerifiedCaptureStatus_SIZE(1);
    ASSERT_EVENTS_VerifiedCaptureStatus(0, 55U, 1U, 91U, Components::PayloadOperationState::ACCEPTED, 0U, 0U, 0U, 0U);

    this->setMissionMode(Components::MissionMode::SAFE);
    this->notifyCapture(1U, 91U, Components::PayloadOperationState::COMPLETED, 61U, bytes, crc, true);
    // The token's terminal answer is FAILED, reason 8: the product exists but was withheld, not sent.
    ASSERT_EVENTS_VerifiedCaptureStatus_SIZE(2);
    ASSERT_EVENTS_VerifiedCaptureStatus(1, 55U, 1U, 91U, Components::PayloadOperationState::FAILED, 61U, bytes, crc,
                                        8U);
    ASSERT_EVENTS_CaptureWithheld_SIZE(1);
    ASSERT_from_scienceProductOut_SIZE(0);
    ASSERT_from_deliveryRequestOut_SIZE(0);
}

void ScienceAppTester::testExistingIdsUnchanged() {
    // Yamcs and the team's scripts know these numbers. The SAFE events are appended after them.
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::OPCODE_START_COLLECTION), 0x0U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::OPCODE_CONFIGURE_CAPTURE_DURATION), 0x1U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::OPCODE_SCIENCE_CAPTURE), 0x2U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::OPCODE_COLLECT_AND_DOWNLINK), 0x3U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::OPCODE_VERIFIED_COLLECT_AND_DOWNLINK), 0x4U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::EVENTID_COLLECTIONTRIGGERED), 0x0U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::EVENTID_SCIENCEPRODUCTREADY), 0x1U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::EVENTID_CAPTUREDURATIONCONFIGURED), 0x2U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::EVENTID_COLLECTIONCANCELLED), 0x3U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::EVENTID_SCIENCECOMMANDREJECTED), 0x4U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::EVENTID_SCIENCEOPERATIONREJECTED), 0x5U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::EVENTID_VERIFIEDCAPTURESTATUS), 0x6U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::CHANNELID_PENDINGDELAYSECONDS), 0x0U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::CHANNELID_CAPTUREDURATIONSECONDS), 0x1U);
    ASSERT_EQ(static_cast<U32>(ScienceAppComponentBase::CHANNELID_COLLECTIONCOUNT), 0x2U);
}

}  // namespace Components
