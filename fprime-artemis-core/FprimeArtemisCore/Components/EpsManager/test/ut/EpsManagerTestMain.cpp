#include "EpsManagerTester.hpp"
#include "STest/Random/Random.hpp"

TEST(EpsManager, NominalAndBusy) { Components::EpsManagerTester tester; tester.nominalAndBusy(); }
TEST(EpsManager, ReadbackAndCycle) { Components::EpsManagerTester tester; tester.readbackAndCycle(); }
TEST(EpsManager, FaultsAndStaleness) { Components::EpsManagerTester tester; tester.faultsAndStaleness(); }
int main(int argc, char** argv) {
    STest::Random::seed();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
