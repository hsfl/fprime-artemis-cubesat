#ifndef Components_EpsManagerTester_HPP
#define Components_EpsManagerTester_HPP

#include "Components/EpsManager/EpsManager.hpp"
#include "Components/EpsManager/EpsManagerGTestBase.hpp"
#include "Components/LinkCfg/LinkCfg.hpp"
#include "../../../../../external/artemis-pdu/src/pdu_protocol_v2.h"

#include <vector>

namespace Components {

class EpsManagerTester final : public EpsManagerGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const U32 TEST_INSTANCE_QUEUE_DEPTH = 10;

    EpsManagerTester();
    ~EpsManagerTester();

    void testCommandWaitsForTerminalResponseAndBusyDoesNotCompleteIt();
    void testNegativeMalformedAndTimeoutStatusesFailCommand();
    void testNamedCommandsAndGuards();
    void testSnapshotStates();

  private:
    struct Request {
        Components::EpsRequest request;
        U8 outputId;
        U8 state;
        U16 durationMs;
    };

    void connectPorts();
    void initComponents();
    void from_driverRequestOut_handler(
        FwIndexType portNum,
        const Components::EpsRequest& request,
        U8 outputId,
        U8 state,
        U16 durationMs
    ) override;
    void sendStatus(
        Components::EpsRequest request,
        U8 linkState,
        U8 driverStatus,
        Components::HealthState health = Components::HealthState::UNKNOWN
    );

    EpsManager component;
    std::vector<Request> m_requests;
};

}  // namespace Components

#endif
