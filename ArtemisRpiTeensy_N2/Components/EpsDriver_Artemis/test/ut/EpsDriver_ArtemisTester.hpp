#ifndef Components_EpsDriver_ArtemisTester_HPP
#define Components_EpsDriver_ArtemisTester_HPP

#include "Components/EpsDriver_Artemis/EpsDriver_Artemis.hpp"
#include "Components/EpsDriver_Artemis/EpsDriver_ArtemisGTestBase.hpp"

#include <vector>

namespace Components {

class EpsDriver_ArtemisTester final : public EpsDriver_ArtemisGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    EpsDriver_ArtemisTester();
    ~EpsDriver_ArtemisTester();

    void testTimeoutClearsPendingRequest();

  private:
    void connectPorts();
    void initComponents();
    void from_teensyRequestOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    EpsDriver_Artemis component;
    std::vector<std::vector<U8> > m_requests;
};

}  // namespace Components

#endif
