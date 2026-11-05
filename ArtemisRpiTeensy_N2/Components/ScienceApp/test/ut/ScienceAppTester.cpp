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
    ASSERT_from_deliveryRequestOut_SIZE(0);  // delivery waits for the storage answer

    this->invoke_to_productStoredIn(0, 24U, true);
    this->component.doDispatch();
    ASSERT_from_deliveryRequestOut_SIZE(1);
    ASSERT_from_deliveryRequestOut(0, 24U);
    ASSERT_TLM_CollectionCount_SIZE(3);
    ASSERT_TLM_CollectionCount(2, 2U);
}

// Run one capture to the point where the product is handed to StorageManager.
void ScienceAppTester::captureToStorage(U32 requestId, U32 productId, U32 generation) {
    static const U8 product[] = {1U, 2U, 3U, 4U};
    ASSERT_TRUE(writeScienceProduct(product, sizeof(product)));
    const Fw::String sourcePath(AUTO_DOWNLINK_TEST_PATH);
    this->sendCmd_SCIENCE_CAPTURE(0, 1, 5U);
    this->component.doDispatch();
    this->invoke_to_operationNoticeIn(0, requestId, generation, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::ACCEPTED, 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0U, true);
    this->component.doDispatch();
    this->invoke_to_operationNoticeIn(0, requestId, generation, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::COMPLETED, productId, sizeof(product),
                                      Components::ScienceProductSource::NEUTRON_SIM, sourcePath,
                                      scienceProductCrc(product, sizeof(product)), true);
    this->component.doDispatch();
}

// SCIENCE_READY means stored, so it waits for StorageManager's answer.
void ScienceAppTester::testScienceReadyWaitsForStorageCommit() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->captureToStorage(1U, 30U, 50U));
    ASSERT_from_scienceProductOut_SIZE(1);
    ASSERT_from_missionModeOut_SIZE(1);
    ASSERT_from_missionModeOut(0, Components::MissionMode::COLLECTING, 30U);

    this->invoke_to_productStoredIn(0, 99U, true);  // not the product we handed over
    this->component.doDispatch();
    ASSERT_from_missionModeOut_SIZE(1);

    this->invoke_to_productStoredIn(0, 30U, true);
    this->component.doDispatch();
    ASSERT_from_missionModeOut_SIZE(2);
    ASSERT_from_missionModeOut(1, Components::MissionMode::SCIENCE_READY, 4U);

    this->invoke_to_productStoredIn(0, 30U, true);  // a repeated answer changes nothing
    this->component.doDispatch();
    ASSERT_from_missionModeOut_SIZE(2);
}

void ScienceAppTester::testStorageFailureReturnsToBaseNotReady() {
    this->clearHistory();
    ASSERT_NO_FATAL_FAILURE(this->captureToStorage(1U, 31U, 51U));
    this->invoke_to_productStoredIn(0, 31U, false);
    this->component.doDispatch();
    ASSERT_EVENTS_ScienceProductNotStored_SIZE(1);
    ASSERT_EVENTS_ScienceProductNotStored(0, 31U);
    ASSERT_from_missionModeOut_SIZE(2);
    ASSERT_from_missionModeOut(1, Components::MissionMode::BASE, 0U);
}

// A capture PayloadManager inhibits is reported as refused. ScienceApp decides nothing itself.
// A product storage refused is never offered for delivery.
void ScienceAppTester::testStorageFailureSendsNoDelivery() {
    static const U8 product[] = {1U, 2U, 3U, 4U};
    ASSERT_TRUE(writeScienceProduct(product, sizeof(product)));
    const Fw::String sourcePath(AUTO_DOWNLINK_TEST_PATH);
    this->clearHistory();
    this->sendCmd_COLLECT_AND_DOWNLINK(0, 1, 5U);
    this->component.doDispatch();
    this->invoke_to_operationNoticeIn(0, 1, 52, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::ACCEPTED, 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, sourcePath, 0U, true);
    this->component.doDispatch();
    this->invoke_to_operationNoticeIn(0, 1, 52, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::COMPLETED, 32U, sizeof(product),
                                      Components::ScienceProductSource::NEUTRON_SIM, sourcePath,
                                      scienceProductCrc(product, sizeof(product)), true);
    this->component.doDispatch();
    this->invoke_to_productStoredIn(0, 32U, false);
    this->component.doDispatch();
    ASSERT_EVENTS_ScienceProductNotStored_SIZE(1);
    ASSERT_from_deliveryRequestOut_SIZE(0);
}

void ScienceAppTester::testInhibitedCaptureIsReportedAndChangesNoMode() {
    this->clearHistory();
    const Fw::String empty("");
    this->sendCmd_SCIENCE_CAPTURE(0, 1, 5U);
    this->component.doDispatch();
    ASSERT_from_payloadRequestOut_SIZE(1);
    this->invoke_to_operationNoticeIn(0, 1U, 0U, Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::INHIBITED, 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, empty, 0U, true);
    this->component.doDispatch();
    ASSERT_EVENTS_ScienceOperationRejected_SIZE(1);
    ASSERT_EVENTS_ScienceOperationRejected(0, 1U, Components::PayloadOperationState::INHIBITED);
    ASSERT_from_missionModeOut_SIZE(0);
}

void ScienceAppTester::testCancelReportsBackToMissionApp() {
    this->clearHistory();
    this->invoke_to_requestIn(0, 20U);
    this->component.doDispatch();
    this->invoke_to_cancelRequestIn(0, 7U);
    this->component.doDispatch();
    ASSERT_from_cancelReportOut_SIZE(1);
    ASSERT_from_cancelReportOut(0, 7U);  // MissionApp matches this key to its SAFE cancel
    for (U32 tick = 0U; tick < 25U; ++tick) {
        this->invoke_to_run(0, 0U);
        this->component.doDispatch();
    }
    ASSERT_from_payloadRequestOut_SIZE(0);
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
