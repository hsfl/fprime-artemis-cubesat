#ifndef Components_AdcsDriver_D2S2_HPP
#define Components_AdcsDriver_D2S2_HPP

#include "Components/AdcsDriver_D2S2/AdcsDriver_D2S2ComponentAc.hpp"

namespace Components {

class AdcsDriver_D2S2 final : public AdcsDriver_D2S2ComponentBase {
  public:
    AdcsDriver_D2S2(const char* const compName);
    ~AdcsDriver_D2S2();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;

    static constexpr U32 STATUS_OFFSET = 300;
    U32 m_lastRequestKey;
};

}  // namespace Components

#endif
