#include "PayloadDriverSelectorTester.hpp"

namespace Components {

PayloadDriverSelectorTester::PayloadDriverSelectorTester()
    : PayloadDriverSelectorGTestBase("PayloadDriverSelectorTester", MAX_HISTORY_SIZE),
      component("PayloadDriverSelector") {
    this->initComponents();
    this->connectPorts();
}

PayloadDriverSelectorTester::~PayloadDriverSelectorTester() {
    this->component.deinit();
}

void PayloadDriverSelectorTester::from_driverRequestOut_handler(FwIndexType portNum, U32 durationSeconds) {
    this->m_lastDriverRequestPort = portNum;
    PayloadDriverSelectorTesterBase::from_driverRequestOut_handler(portNum, durationSeconds);
}

void PayloadDriverSelectorTester::from_deactivateDriverOut_handler(FwIndexType portNum) {
    this->m_lastDeactivatedDriverPort = portNum;
    PayloadDriverSelectorTesterBase::from_deactivateDriverOut_handler(portNum);
}

void PayloadDriverSelectorTester::testLeptonDefaultAndCompletion() {
    this->clearHistory();
    this->invoke_to_requestIn(0, 7U);
    this->component.doDispatch();

    ASSERT_from_driverRequestOut_SIZE(1);
    ASSERT_from_driverRequestOut(0, 7U);
    ASSERT_EQ(this->m_lastDriverRequestPort, 0);
    ASSERT_TLM_SelectedDriver(0, Components::PayloadDriverKind::LEPTON);
    ASSERT_TLM_RequestInFlight(0, 1U);

    const Fw::String path("DpCat/lepton.fdp");
    this->invoke_to_driverStatusIn(
        0,
        11U,
        38480U,
        Components::ScienceProductSource::REAL_PAYLOAD,
        path,
        0x1234U
    );
    this->component.doDispatch();

    ASSERT_from_statusOut_SIZE(1);
    ASSERT_from_statusOut(
        0,
        11U,
        38480U,
        Components::ScienceProductSource::REAL_PAYLOAD,
        path,
        0x1234U
    );
    ASSERT_TLM_RequestInFlight(1, 0U);
}

void PayloadDriverSelectorTester::testBosonSelectionAndCompletion() {
    this->clearHistory();
    this->sendCmd_SELECT_PAYLOAD_DRIVER(
        0,
        1,
        Components::PayloadDriverKind::BOSON
    );
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE(
        0,
        PayloadDriverSelectorComponentBase::OPCODE_SELECT_PAYLOAD_DRIVER,
        1,
        Fw::CmdResponse::OK
    );
    ASSERT_from_deactivateDriverOut_SIZE(1);
    ASSERT_EQ(this->m_lastDeactivatedDriverPort, 0);

    this->invoke_to_requestIn(0, 9U);
    this->component.doDispatch();
    ASSERT_from_driverRequestOut_SIZE(1);
    ASSERT_from_driverRequestOut(0, 9U);
    ASSERT_EQ(this->m_lastDriverRequestPort, 1);

    const Fw::String path("DpCat/Boson_1.fdp");
    this->invoke_to_driverStatusIn(
        1,
        22U,
        163922U,
        Components::ScienceProductSource::BOSON,
        path,
        0x5678U
    );
    this->component.doDispatch();
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_TLM_RequestInFlight_SIZE(3);
    ASSERT_TLM_RequestInFlight(2, 0U);
}

void PayloadDriverSelectorTester::testSelectionRejectedWhileBusy() {
    this->clearHistory();
    this->invoke_to_requestIn(0, 5U);
    this->component.doDispatch();
    this->sendCmd_SELECT_PAYLOAD_DRIVER(
        0,
        2,
        Components::PayloadDriverKind::BOSON
    );
    this->component.doDispatch();

    ASSERT_CMD_RESPONSE(
        0,
        PayloadDriverSelectorComponentBase::OPCODE_SELECT_PAYLOAD_DRIVER,
        2,
        Fw::CmdResponse::BUSY
    );
    ASSERT_EVENTS_PayloadSelectionRejected_SIZE(1);
}

void PayloadDriverSelectorTester::testDuplicateRequestDoesNotTerminateActiveCapture() {
    this->clearHistory();
    this->invoke_to_requestIn(0, 5U);
    this->component.doDispatch();
    this->invoke_to_requestIn(0, 5U);
    this->component.doDispatch();

    ASSERT_from_driverRequestOut_SIZE(1);
    ASSERT_from_statusOut_SIZE(0);
    ASSERT_EVENTS_PayloadSelectionRejected_SIZE(1);
    ASSERT_TLM_RequestInFlight_SIZE(1);
    ASSERT_TLM_RequestInFlight(0, 1U);
}

void PayloadDriverSelectorTester::testNonSelectedStatusIgnored() {
    this->clearHistory();
    this->invoke_to_requestIn(0, 5U);
    this->component.doDispatch();

    const Fw::String path("DpCat/Boson_ignored.fdp");
    this->invoke_to_driverStatusIn(
        1,
        22U,
        163922U,
        Components::ScienceProductSource::BOSON,
        path,
        0x5678U
    );
    this->component.doDispatch();

    ASSERT_from_statusOut_SIZE(0);
    ASSERT_EVENTS_PayloadStatusIgnored_SIZE(1);
    ASSERT_TLM_RequestInFlight_SIZE(1);
    ASSERT_TLM_RequestInFlight(0, 1U);
}

}  // namespace Components
