#ifndef Components_AdcsAdapter_D2S2_HPP
#define Components_AdcsAdapter_D2S2_HPP

#include "Components/AdcsAdapter_D2S2/AdcsAdapter_D2S2ComponentAc.hpp"

namespace Components {

class AdcsAdapter_D2S2 final : public AdcsAdapter_D2S2ComponentBase {
  public:
    AdcsAdapter_D2S2(const char* const compName);
    ~AdcsAdapter_D2S2();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;

    static constexpr U32 STATUS_OFFSET = 300;
    U32 m_lastRequestKey;
};

}  // namespace Components

#endif
