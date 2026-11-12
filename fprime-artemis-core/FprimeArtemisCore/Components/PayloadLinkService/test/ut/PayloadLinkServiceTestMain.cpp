#include "PayloadLinkServiceTester.hpp"
TEST(PayloadLinkService, CaptureAdmissionAndDeduplication) {
    Components::PayloadLinkServiceTester tester; tester.captureAdmissionAndDeduplication();
}
TEST(PayloadLinkService, CommitAndStage) {
    Components::PayloadLinkServiceTester tester; tester.committedProductStagesOnlyOnMatchingAcks();
}
TEST(PayloadLinkService, RetryTimeout) {
    Components::PayloadLinkServiceTester tester; tester.stageTimeoutIsBounded();
}
TEST(PayloadLinkService, PersistentOlderProductAfterFailureAndRestart) {
    Components::PayloadLinkServiceTester tester; tester.retainedProductsSurviveFailuresAndRestart();
}
TEST(PayloadLinkService, CompletedStageReplay) {
    Components::PayloadLinkServiceTester tester; tester.completedStageIsIdempotent();
}
TEST(PayloadLinkService, CancellationRecovery) {
    Components::PayloadLinkServiceTester tester; tester.cancellationWaitsForDriverThenRecovers();
}
TEST(PayloadLinkService, PendingResultCannotReleaseCancellation) {
    Components::PayloadLinkServiceTester tester; tester.pendingResultCannotReleaseCancellation();
}
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
