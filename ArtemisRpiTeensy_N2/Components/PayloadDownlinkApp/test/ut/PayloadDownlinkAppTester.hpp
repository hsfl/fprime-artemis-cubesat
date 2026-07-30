#ifndef Components_PayloadDownlinkAppTester_HPP
#define Components_PayloadDownlinkAppTester_HPP

#include "Components/PayloadDownlinkApp/PayloadDownlinkApp.hpp"
#include "Components/PayloadDownlinkApp/PayloadDownlinkAppGTestBase.hpp"

#include <vector>

namespace Components {

class PayloadDownlinkAppTester final : public PayloadDownlinkAppGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 128;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    PayloadDownlinkAppTester();
    ~PayloadDownlinkAppTester();

    void testHeaderRetransmitBehavior();
    void testBurstCountSendsGeneratedPayloadPacketsPerRun();
    void testProgressTelemetryAndExplicitStatus();
    void testRetryBurstCountSendsGeneratedRetryPacketsPerRun();
    void testActiveRequestGuardPreservesTransferAndProgress();
    void testControlMailboxCopiesInputAndReportsOverflow();
    void testRetryRequestsMergeAdditivelyAndIgnoreEmptyRequest();
    void testAbortClearsPendingRepairAndRejectsLaterControl();
    void testLocalRetryPreservesNominalAndEndProgress();
    void testRepairRetryPreservesCursor();
    void testLocalErrorFailsWithoutAdvance();
    void testInitializationFailurePublishesCorrelatedIdentity();
    void testRepairsDoNotStarveNominalProgress();
    void testMalformedControlIsRejectedWithoutPoisoningStatus();
    void testOnDemandCacheTransaction();
    void testCacheRequestRetriesWithoutAdvancing();
    void testConflictingRequestDoesNotRestartUpload();

  private:
    void connectPorts();
    void initComponents();
    Components::PayloadSendStatus from_packetOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    Components::PayloadSendStatus from_cacheRequestOut_handler(FwIndexType portNum,
                                                               Fw::Buffer& fwBuffer) override;
    void writePayloadFile(const U8* data, FwSizeType size);
    void rejectNextPacket(U8 packetType, const Components::PayloadSendStatus& status, U32 count = 1U);
    void respondToLastCacheRequest(U8 state, U32 nextOffset, U8 status = LinkCfg::TEENSY_STATUS_OK);

    PayloadDownlinkApp component;
    U8 m_rejectedPacketType;
    Components::PayloadSendStatus m_rejectedStatus;
    U32 m_rejectionsRemaining;
    std::vector<std::vector<U8> > m_packets;
    std::vector<std::vector<U8> > m_cacheRequests;
};

}  // namespace Components

#endif
