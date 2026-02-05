#ifndef Components_TeensyLink_HPP
#define Components_TeensyLink_HPP

#include "Components/TeensyLink/TeensyLinkComponentAc.hpp"

namespace Components {

class TeensyLink final : public TeensyLinkComponentBase {
  public:
    TeensyLink(const char* const compName);
    ~TeensyLink();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void RESET_COUNTERS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    U32 m_linkHeartbeat;
    U32 m_framingDrops;
    U32 m_timeoutEvents;
};

}  // namespace Components

#endif
