#ifndef Components_CommsAdapter_TeensyRfm23_HPP
#define Components_CommsAdapter_TeensyRfm23_HPP

#include "Components/CommsAdapter_TeensyRfm23/CommsAdapter_TeensyRfm23ComponentAc.hpp"
#include "Components/LinkCfg/LinkCfg.hpp"

namespace Components {

class CommsAdapter_TeensyRfm23 final : public CommsAdapter_TeensyRfm23ComponentBase {
  public:
    CommsAdapter_TeensyRfm23(const char* const compName);
    ~CommsAdapter_TeensyRfm23();

  private:
    enum class LinkState : U32 {
        DOWN = 0,
        ACQUIRING = 1,
        LOCKED = 2,
        DEGRADED = 3,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;
    void teensyResponseIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    void applyTransportPoll(U32 pollKey);
    void emitStatus();
    bool sendRfStatsRequest();
    bool parseRfStatsResponse(const U8* data, FwSizeType size);
    U32 toStatusKey() const;
    static U16 readLe16(const U8* data);
    static U32 readLe32(const U8* data);

    U32 m_lastRequestKey;
    U32 m_requestCount;
    U8 m_sequence;
    LinkState m_linkState;
    I32 m_rssiDbm;
    U32 m_rfRxPackets;
    U32 m_rfTxPackets;
    U32 m_rfTxDrops;
    U8 m_localPacket[5];
};

}  // namespace Components

#endif
