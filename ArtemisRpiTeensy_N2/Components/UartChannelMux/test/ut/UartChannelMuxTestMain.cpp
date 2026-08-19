#include "UartChannelMuxTester.hpp"

TEST(Nominal, WrapsAndRoutesChannelFrames) {
    Components::UartChannelMuxTester tester;
    tester.testWrapsAndRoutesChannelFrames();
}

TEST(Nominal, PropagatesPayloadLocalAcceptanceStatus) {
    Components::UartChannelMuxTester tester;
    tester.testPropagatesPayloadLocalAcceptanceStatus();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
