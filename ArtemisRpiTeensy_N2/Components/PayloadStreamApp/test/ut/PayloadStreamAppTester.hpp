#ifndef Components_PayloadStreamAppTester_HPP
#define Components_PayloadStreamAppTester_HPP

#include "Components/PayloadStreamApp/PayloadStreamApp.hpp"
#include "Components/PayloadStreamApp/PayloadStreamAppGTestBase.hpp"

#include <vector>

namespace Components {

class PayloadStreamAppTester final : public PayloadStreamAppGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 128;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    PayloadStreamAppTester();
    ~PayloadStreamAppTester();

    void testResponsePacedUploadStartsNextFrameImmediately();
    void testTargetFailureDropsWithoutRetry();
    void testLostResponseTimesOutAndAdvancesToNewFrame();

  private:
    void connectPorts();
    void initComponents();
    void respondToLastRequest(U8 status, U32 receivedBytes);
    void from_previewRequestOut_handler(FwIndexType portNum) override;
    void from_responseAdvanceOut_handler(FwIndexType portNum) override;
    Components::PayloadSendStatus from_previewPacketOut_handler(FwIndexType portNum,
                                                                Fw::Buffer& packet) override;

    PayloadStreamApp component;
    U8 m_preview[80U * 60U];
    std::vector<std::vector<U8> > m_packets;
};

}  // namespace Components

#endif
