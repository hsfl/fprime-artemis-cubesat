#ifndef Components_PayloadDownlinkManagerTester_HPP
#define Components_PayloadDownlinkManagerTester_HPP

#include "Components/PayloadDownlinkManager/PayloadDownlinkManager.hpp"
#include "Components/PayloadDownlinkManager/PayloadDownlinkManagerGTestBase.hpp"

#include <vector>

namespace Components {

class PayloadDownlinkManagerTester final : public PayloadDownlinkManagerGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    PayloadDownlinkManagerTester();
    ~PayloadDownlinkManagerTester();

    void testFileBackedVariableLengthPackets();
    void testQueuesRetryPacketsForScheduledResend();

  private:
    void connectPorts();
    void initComponents();
    void from_packetOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void writePayloadFile(const U8* data, FwSizeType size);

    PayloadDownlinkManager component;
    std::vector<std::vector<U8> > m_packets;
};

}  // namespace Components

#endif
