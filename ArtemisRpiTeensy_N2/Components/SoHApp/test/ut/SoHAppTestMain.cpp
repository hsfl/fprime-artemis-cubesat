#include "SoHAppTester.hpp"

TEST(Report, EmitSnapshotReportsEverySlot) {
    Components::SoHAppTester tester;
    tester.testEmitSnapshotReportsEverySlot();
}

TEST(Report, OverallIgnoresMissingOptionalSlots) {
    Components::SoHAppTester tester;
    tester.testOverallIgnoresMissingOptionalSlots();
}

TEST(Report, OverallUnknownWhenRequiredSlotSilent) {
    Components::SoHAppTester tester;
    tester.testOverallUnknownWhenRequiredSlotSilent();
}

TEST(Report, FailThenWarnWin) {
    Components::SoHAppTester tester;
    tester.testFailThenWarnWin();
}

TEST(Report, RequiredSlotsParameter) {
    Components::SoHAppTester tester;
    tester.testRequiredSlotsParameter();
}

TEST(Periodic, OffByDefaultSendsNothing) {
    Components::SoHAppTester tester;
    tester.testReportingOffByDefaultSendsNothing();
}

TEST(Periodic, OnSendsNowThenEveryPeriod) {
    Components::SoHAppTester tester;
    tester.testReportingOnSendsNowThenEveryPeriod();
}

TEST(Periodic, OffStops) {
    Components::SoHAppTester tester;
    tester.testReportingOffStops();
}

TEST(Periodic, PeriodParameter) {
    Components::SoHAppTester tester;
    tester.testPeriodParameter();
}

TEST(Periodic, BadPeriodRejectedAndDefaultUsed) {
    Components::SoHAppTester tester;
    tester.testBadPeriodRejectedAndDefaultUsed();
}

TEST(Periodic, StatusChangeShowsInNextReport) {
    Components::SoHAppTester tester;
    tester.testStatusChangeShowsInNextReport();
}

TEST(Ids, ExistingIdsUnchanged) {
    Components::SoHAppTester tester;
    tester.testExistingIdsUnchanged();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
