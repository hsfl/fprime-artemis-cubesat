#include "PayloadDownlinkAppTester.hpp"

TEST(Nominal, OnDemandCacheTransaction) {
    Components::PayloadDownlinkAppTester tester;
    tester.testOnDemandCacheTransaction();
}

TEST(Reliability, CacheRequestRetriesWithoutAdvancing) {
    Components::PayloadDownlinkAppTester tester;
    tester.testCacheRequestRetriesWithoutAdvancing();
}

TEST(Reliability, ConflictingRequestDoesNotRestartUpload) {
    Components::PayloadDownlinkAppTester tester;
    tester.testConflictingRequestDoesNotRestartUpload();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
