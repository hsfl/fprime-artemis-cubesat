#ifndef Components_EpsAdapter_Artemis_HPP
#define Components_EpsAdapter_Artemis_HPP

#include "Components/EpsAdapter_Artemis/EpsAdapter_ArtemisComponentAc.hpp"

namespace Components {

class EpsAdapter_Artemis final : public EpsAdapter_ArtemisComponentBase {
  public:
    EpsAdapter_Artemis(const char* const compName);
    ~EpsAdapter_Artemis();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;

    static constexpr U32 STATUS_OFFSET = 100;
    U32 m_lastRequestKey;
};

}  // namespace Components

#endif
