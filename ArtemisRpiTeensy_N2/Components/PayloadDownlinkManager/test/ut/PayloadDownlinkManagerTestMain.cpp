#include "PayloadDownlinkManagerTester.hpp"

TEST(Nominal, FileBackedVariableLengthPackets) {
    Components::PayloadDownlinkManagerTester tester;
    tester.testFileBackedVariableLengthPackets();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
