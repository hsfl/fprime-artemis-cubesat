#ifndef Components_UartChannelMuxTester_HPP
#define Components_UartChannelMuxTester_HPP

#include "Components/UartChannelMux/UartChannelMux.hpp"
#include "Components/UartChannelMux/UartChannelMuxGTestBase.hpp"

#include <vector>

namespace Components {

class UartChannelMuxTester final : public UartChannelMuxGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 128;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    UartChannelMuxTester();
    ~UartChannelMuxTester();

    void testWrapsAndRoutesChannelFrames();
    void testPropagatesPayloadLocalAcceptanceStatus();

  private:
    void connectPorts();
    void initComponents();
    Drv::ByteStreamStatus from_drvSendOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void from_ccsdsRecvOut_handler(
        FwIndexType portNum,
        Fw::Buffer& fwBuffer,
        const Drv::ByteStreamStatus& status
    ) override;
    void from_payloadRecvOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void from_localRecvOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void from_previewRecvOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    static std::vector<U8> makeFrame(U8 channel, const U8* payload, FwSizeType size);
    static U16 crc16Ccitt(const U8* data, FwSizeType size);

    UartChannelMux component;
    Drv::ByteStreamStatus m_drvSendStatus;
    std::vector<std::vector<U8> > m_txFrames;
    std::vector<std::vector<U8> > m_ccsdsFrames;
    std::vector<std::vector<U8> > m_payloadFrames;
    std::vector<std::vector<U8> > m_localFrames;
    std::vector<std::vector<U8> > m_previewFrames;
};

}  // namespace Components

#endif
