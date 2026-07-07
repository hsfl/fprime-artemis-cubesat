#include "EpsDriver_ArtemisTester.hpp"

TEST(Nominal, TimeoutClearsPendingRequest) {
    Components::EpsDriver_ArtemisTester tester;
    tester.testTimeoutClearsPendingRequest();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
