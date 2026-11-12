#include "ScienceCoordinatorTester.hpp"
#define SCIENCE_TEST(Name, Method) TEST(ScienceCoordinator, Name) { Components::ScienceCoordinatorTester tester; tester.Method(); }
SCIENCE_TEST(AdmissionAndReadiness, admissionAndReadiness)
SCIENCE_TEST(FullSizeCommitAndDownlink, completeMaximumProduct)
SCIENCE_TEST(CorruptedStage, corruptedProductRefused)
SCIENCE_TEST(DuplicateCommitDuringDownlink, duplicateCommitDuringDownlink)
SCIENCE_TEST(StaleErrorAfterSend, staleErrorAfterSend)
SCIENCE_TEST(PiIndependentRetainedCache, cancelRetainsPiIndependentCache)
SCIENCE_TEST(LateProductAfterTimeout, lateProductAfterTimeout)
SCIENCE_TEST(BoundedRadioFailure, localRadioFailureIsBounded)
SCIENCE_TEST(FailedStageAborts, failedStageAbortsAndInvalidates)
SCIENCE_TEST(NewScheduleRetiresRepair, newScheduleRetiresOldRepair)
SCIENCE_TEST(ExplicitRestageAfterReset, explicitRestageAfterReset)
int main(int argc, char** argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
