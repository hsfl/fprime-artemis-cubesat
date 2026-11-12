#ifndef ARTEMIS_PAYLOAD_LINK_SERVICE_TESTER_HPP
#define ARTEMIS_PAYLOAD_LINK_SERVICE_TESTER_HPP
#include "FprimeArtemisCore/Components/PayloadLinkService/PayloadLinkService.hpp"
#include "FprimeArtemisCore/Components/PayloadLinkService/PayloadLinkServiceGTestBase.hpp"
#include <string>

namespace Components {
class PayloadLinkServiceTester : public PayloadLinkServiceGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 4096;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    explicit PayloadLinkServiceTester(const std::string& root = "");
    ~PayloadLinkServiceTester();
    void captureAdmissionAndDeduplication();
    void committedProductStagesOnlyOnMatchingAcks();
    void stageTimeoutIsBounded();
    void retainedProductsSurviveFailuresAndRestart();
    void completedStageIsIdempotent();
    void cancellationWaitsForDriverThenRecovers();
    void pendingResultCannotReleaseCancellation();
  private:
    void connectPorts();
    void initComponents();
    void tick(unsigned count = 1);
    TwinScience::Frame request();
    static std::string createDirectory(const std::string& existing);
    void commit(U32 id, const std::string& name);
    void removeProducts();
    std::string directory;
    bool owner;
    PayloadLinkService component;
};
}
#endif
