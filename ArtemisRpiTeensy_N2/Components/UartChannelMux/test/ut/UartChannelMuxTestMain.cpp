#include "UartChannelMuxTester.hpp"

TEST(Nominal, WrapsAndRoutesChannelFrames) {
    Components::UartChannelMuxTester tester;
    tester.testWrapsAndRoutesChannelFrames();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
