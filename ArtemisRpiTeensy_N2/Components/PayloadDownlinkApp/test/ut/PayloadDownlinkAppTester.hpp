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
    void testProgressEventsEveryTenPercent();
    void testRetryBurstCountSendsGeneratedRetryPacketsPerRun();

  private:
    void connectPorts();
    void initComponents();
    void from_packetOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void writePayloadFile(const U8* data, FwSizeType size);

    PayloadDownlinkApp component;
    std::vector<std::vector<U8> > m_packets;
};

}  // namespace Components

#endif
