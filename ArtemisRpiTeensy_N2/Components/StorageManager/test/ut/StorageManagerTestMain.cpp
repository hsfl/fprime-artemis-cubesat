#include "StorageManagerTester.hpp"

TEST(CommitResult, CommittedProductIsReported) {
    Components::StorageManagerTester tester;
    tester.testCommittedProductIsReported();
}

TEST(CommitResult, BadChecksumIsReportedNotStored) {
    Components::StorageManagerTester tester;
    tester.testBadChecksumIsReportedNotStored();
}

TEST(CommitResult, BadDescriptorIsReportedNotStored) {
    Components::StorageManagerTester tester;
    tester.testBadDescriptorIsReportedNotStored();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
