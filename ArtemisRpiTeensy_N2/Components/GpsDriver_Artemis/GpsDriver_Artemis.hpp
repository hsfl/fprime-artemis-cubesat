#ifndef Components_GpsDriver_Artemis_HPP
#define Components_GpsDriver_Artemis_HPP

#include "Components/GpsDriver_Artemis/GpsDriver_ArtemisComponentAc.hpp"

namespace Components {

class GpsDriver_Artemis final : public GpsDriver_ArtemisComponentBase {
  public:
    GpsDriver_Artemis(const char* const compName);
    ~GpsDriver_Artemis();

  private:
    enum class FixState : U32 {
        NO_FIX = 0,
        ACQUIRING = 1,
        FIX_2D = 2,
        FIX_3D = 3,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;

    void updateFixModel();
    U32 toStatusKey() const;

    U32 m_lastRequestKey;
    U32 m_requestCount;
    FixState m_fixState;
    U32 m_satellitesTracked;
    U32 m_fixQualityScore;
};

}  // namespace Components

#endif
