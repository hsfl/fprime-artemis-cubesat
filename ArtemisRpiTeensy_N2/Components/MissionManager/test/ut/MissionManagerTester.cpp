#include "MissionManagerTester.hpp"

namespace Components {

MissionManagerTester::MissionManagerTester()
    : MissionManagerGTestBase("MissionManagerTester", MAX_HISTORY_SIZE),
      component("MissionManager") {
    this->initComponents();
    this->connectPorts();
}

MissionManagerTester::~MissionManagerTester() {
    this->component.deinit();
}

void MissionManagerTester::testRejectsInvalidServiceTransition() {
    this->clearHistory();

    this->invoke_to_modeUpdateIn(0, Components::MissionMode::SCIENCE_READY, 42);

    ASSERT_EVENTS_ModeUpdateRejected_SIZE(1);
    ASSERT_EVENTS_ModeUpdateRejected(
        0,
        Components::MissionMode::SCIENCE_READY,
        Components::MissionMode::BASE,
        42);
    ASSERT_EVENTS_ModeChanged_SIZE(0);

    for (U32 tick = 0; tick < 30; ++tick) {
        this->invoke_to_run(0, 0);
    }
    ASSERT_TLM_CurrentMode(0, Components::MissionMode::BASE);
}

void MissionManagerTester::testAcceptsNominalDemoStoryTransitions() {
    this->clearHistory();

    this->sendCmd_SCHEDULE_COLLECTION(0, 0, 10);
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_EVENTS_ModeChanged_SIZE(1);
    ASSERT_EVENTS_ModeChanged(0, Components::MissionMode::COLLECTION_PENDING);
    ASSERT_EVENTS_CollectionScheduled_SIZE(1);
    ASSERT_EVENTS_CollectionScheduled(0, 10);
    ASSERT_from_collectionRequestOut_SIZE(1);
    ASSERT_from_collectionRequestOut(0, 10);

    this->invoke_to_modeUpdateIn(0, Components::MissionMode::COLLECTING, 10);
    this->invoke_to_modeUpdateIn(0, Components::MissionMode::SCIENCE_READY, 128);
    this->invoke_to_modeUpdateIn(1, Components::MissionMode::DOWNLINKING, 128);
    this->invoke_to_modeUpdateIn(1, Components::MissionMode::BASE, 0);

    ASSERT_EVENTS_ModeUpdateRejected_SIZE(0);
    ASSERT_EVENTS_ModeChanged_SIZE(5);
    ASSERT_EVENTS_ModeChanged(1, Components::MissionMode::COLLECTING);
    ASSERT_EVENTS_ModeChanged(2, Components::MissionMode::SCIENCE_READY);
    ASSERT_EVENTS_ModeChanged(3, Components::MissionMode::DOWNLINKING);
    ASSERT_EVENTS_ModeChanged(4, Components::MissionMode::BASE);

    ASSERT_TLM_CurrentMode(0, Components::MissionMode::COLLECTION_PENDING);
    ASSERT_TLM_CurrentMode(4, Components::MissionMode::BASE);
    ASSERT_TLM_LastScheduledDelaySeconds(4, 10);
}

void MissionManagerTester::testAcceptsManualDownlinkRetryFromBase() {
    this->clearHistory();

    this->invoke_to_modeUpdateIn(1, Components::MissionMode::DOWNLINKING, 128);

    ASSERT_EVENTS_ModeUpdateRejected_SIZE(0);
    ASSERT_EVENTS_ModeChanged_SIZE(1);
    ASSERT_EVENTS_ModeChanged(0, Components::MissionMode::DOWNLINKING);

    this->invoke_to_modeUpdateIn(1, Components::MissionMode::BASE, 0);

    ASSERT_EVENTS_ModeChanged_SIZE(2);
    ASSERT_EVENTS_ModeChanged(1, Components::MissionMode::BASE);
}

}  // namespace Components
