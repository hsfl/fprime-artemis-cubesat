#ifndef Components_GpsAdapter_Artemis_HPP
#define Components_GpsAdapter_Artemis_HPP

#include "Components/GpsAdapter_Artemis/GpsAdapter_ArtemisComponentAc.hpp"

namespace Components {

class GpsAdapter_Artemis final : public GpsAdapter_ArtemisComponentBase {
  public:
    GpsAdapter_Artemis(const char* const compName);
    ~GpsAdapter_Artemis();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;

    static constexpr U32 STATUS_OFFSET = 400;
    U32 m_lastRequestKey;
};

}  // namespace Components

#endif
