#ifndef Components_PingResponder_HPP
#define Components_PingResponder_HPP

#include "Components/PingResponder/PingResponderComponentAc.hpp"

namespace Components {

class PingResponder final : public PingResponderComponentBase {
  public:
    PingResponder(const char* const compName);
    ~PingResponder();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
};

}  // namespace Components

#endif
