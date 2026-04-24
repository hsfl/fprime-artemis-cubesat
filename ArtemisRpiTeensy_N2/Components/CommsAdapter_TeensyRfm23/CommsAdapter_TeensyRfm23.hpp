#ifndef Components_CommsAdapter_TeensyRfm23_HPP
#define Components_CommsAdapter_TeensyRfm23_HPP

#include "Components/CommsAdapter_TeensyRfm23/CommsAdapter_TeensyRfm23ComponentAc.hpp"

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

    void applyTransportPoll(U32 pollKey);
    U32 toStatusKey() const;

    U32 m_lastRequestKey;
    U32 m_requestCount;
    LinkState m_linkState;
    I32 m_rssiDbm;
    U32 m_rfRxPackets;
    U32 m_rfTxPackets;
    U32 m_rfTxDrops;
};

}  // namespace Components

#endif
