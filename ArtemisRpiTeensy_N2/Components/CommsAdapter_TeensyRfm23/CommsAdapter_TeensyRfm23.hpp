#ifndef Components_CommsAdapter_TeensyRfm23_HPP
#define Components_CommsAdapter_TeensyRfm23_HPP

#include "Components/CommsAdapter_TeensyRfm23/CommsAdapter_TeensyRfm23ComponentAc.hpp"

namespace Components {

class CommsAdapter_TeensyRfm23 final : public CommsAdapter_TeensyRfm23ComponentBase {
  public:
    CommsAdapter_TeensyRfm23(const char* const compName);
    ~CommsAdapter_TeensyRfm23();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;

    static constexpr U32 STATUS_OFFSET = 500;
    U32 m_lastRequestKey;
};

}  // namespace Components

#endif
