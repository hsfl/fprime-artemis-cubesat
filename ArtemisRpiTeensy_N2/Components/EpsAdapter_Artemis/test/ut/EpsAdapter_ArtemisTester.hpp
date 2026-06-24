#ifndef Components_EpsAdapter_ArtemisTester_HPP
#define Components_EpsAdapter_ArtemisTester_HPP

#include "Components/EpsAdapter_Artemis/EpsAdapter_Artemis.hpp"
#include "Components/EpsAdapter_Artemis/EpsAdapter_ArtemisGTestBase.hpp"

#include <vector>

namespace Components {

class EpsAdapter_ArtemisTester final : public EpsAdapter_ArtemisGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    EpsAdapter_ArtemisTester();
    ~EpsAdapter_ArtemisTester();

    void testTimeoutClearsPendingRequest();

  private:
    void connectPorts();
    void initComponents();
    void from_teensyRequestOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    EpsAdapter_Artemis component;
    std::vector<std::vector<U8> > m_requests;
};

}  // namespace Components

#endif
