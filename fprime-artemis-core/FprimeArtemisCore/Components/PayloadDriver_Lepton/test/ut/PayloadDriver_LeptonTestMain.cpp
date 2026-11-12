#include "PayloadDriver_LeptonTester.hpp"
TEST(PayloadDriverLepton, CancelLongCaptureThenFresh) { Components::PayloadDriver_LeptonTester t; t.cancelLongCaptureThenFresh(); }
TEST(PayloadDriverLepton, CancelPendingWrite) { Components::PayloadDriver_LeptonTester t; t.cancelPendingWrite(); }
TEST(PayloadDriverLepton, CancelledWriteTimeoutRejectsLateResult) { Components::PayloadDriver_LeptonTester t; t.cancelledWriteTimeoutRejectsLateResult(); }
TEST(PayloadDriverLepton, CompletionRacingCancel) { Components::PayloadDriver_LeptonTester t; t.completionRacingCancel(); }
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
