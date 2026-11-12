#include "PayloadDriver_LeptonTester.hpp"
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace Components {
PayloadDriver_LeptonTester::PayloadDriver_LeptonTester()
    : PayloadDriver_LeptonGTestBase("Tester", MAX_HISTORY_SIZE), component("Lepton") {
    char cwd[4096] = {};
    FW_ASSERT(getcwd(cwd, sizeof(cwd)) != nullptr);
    previousDirectory = cwd;
    char name[] = "/tmp/artemis-lepton-cancel-XXXXXX";
    const char* root = mkdtemp(name);
    FW_ASSERT(root != nullptr);
    directory = root;
    FW_ASSERT(chdir(directory.c_str()) == 0);
    FW_ASSERT(mkdir("DpCat", 0700) == 0);
    setenv("LEPTON_CAMERA_BACKEND", "synthetic", 1);
    initComponents(); connectPorts(); dispatchRun(); clearHistory();
}
PayloadDriver_LeptonTester::~PayloadDriver_LeptonTester() {
    component.deinit();
    for (const auto& file : files) std::remove(file.c_str());
    rmdir("DpCat");
    (void)chdir(previousDirectory.c_str());
    rmdir(directory.c_str());
    unsetenv("LEPTON_CAMERA_BACKEND");
}
Fw::Success::T PayloadDriver_LeptonTester::productGet_handler(FwDpIdType, FwSizeType size,
                                                                 Fw::Buffer& result) {
    buffer.resize(Fw::DpContainer::MIN_PACKET_SIZE + size);
    result = Fw::Buffer(buffer.data(), static_cast<FwSizeType>(buffer.size()));
    return Fw::Success::SUCCESS;
}
void PayloadDriver_LeptonTester::productSend_handler(FwDpIdType, const Fw::Buffer& sent) {
    EXPECT_EQ(sent.getData(), buffer.data());
    EXPECT_GT(sent.getSize(), 160U * 120U * sizeof(U16));
}
void PayloadDriver_LeptonTester::from_cancelDoneOut_handler(FwIndexType) {
    EXPECT_EQ(component.m_pendingWrites, 0U);
    EXPECT_FALSE(component.m_captureWindow.active());
    EXPECT_EQ(component.m_state, PayloadState::READY);
    EXPECT_EQ(fromPortHistory_statusOut->size(), 1U);
    if (fromPortHistory_statusOut->size() == 1U) EXPECT_EQ(fromPortHistory_statusOut->at(0).productBytes, 0U);
    pushFromPortEntry_cancelDoneOut();
}
void PayloadDriver_LeptonTester::dispatchRun() {
    invoke_to_run(0, 0); component.doDispatch();
}
void PayloadDriver_LeptonTester::completeInterval() {
    // Advance only the nonblocking interval; no wall-clock sleeps in this UT.
    component.m_captureWindow.finish();
    ASSERT_TRUE(component.m_captureWindow.start(0, 1));
    dispatchRun();
    ASSERT_EQ(component.m_pendingWrites, 1U);
}
void PayloadDriver_LeptonTester::acknowledge(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    file.close(); files.push_back(path);
    invoke_to_dpWrittenIn(0, Fw::String(path.c_str()), 10, static_cast<FwSizeType>(buffer.size()));
    component.doDispatch();
}
void PayloadDriver_LeptonTester::startShortCapture() {
    invoke_to_requestIn(0, 1); component.doDispatch();
    ASSERT_TRUE(component.m_captureWindow.active());
    completeInterval();
}
void PayloadDriver_LeptonTester::cancelLongCaptureThenFresh() {
    invoke_to_requestIn(0, 3600); component.doDispatch();
    ASSERT_EQ(component.m_state, PayloadState::BUSY);
    invoke_to_pingIn(0, 42); component.doDispatch(); ASSERT_from_pingOut(0, 42);
    invoke_to_cancelIn(0);
    ASSERT_TRUE(component.m_captureWindow.active()); // Cancellation is task-serialized.
    component.doDispatch();
    ASSERT_FALSE(component.m_captureWindow.active());
    ASSERT_EQ(component.m_state, PayloadState::READY);
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_from_cancelDoneOut_SIZE(1);
    ASSERT_EQ(fromPortHistory_statusOut->at(0).productBytes, 0U);
    clearHistory(); startShortCapture();
    acknowledge(component.m_pendingPath);
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_GT(fromPortHistory_statusOut->at(0).productBytes, 38400U);
    ASSERT_EQ(fromPortHistory_statusOut->at(0).sourceKind, ScienceProductSource::REAL_PAYLOAD);
    ASSERT_EQ(component.m_state, PayloadState::READY);
    ASSERT_from_cancelDoneOut_SIZE(0);
}
void PayloadDriver_LeptonTester::cancelPendingWrite() {
    startShortCapture(); const auto old = component.m_pendingPath;
    clearHistory(); invoke_to_cancelIn(0); component.doDispatch();
    ASSERT_EQ(component.m_state, PayloadState::BUSY); ASSERT_from_statusOut_SIZE(0);
    ASSERT_from_cancelDoneOut_SIZE(0);
    invoke_to_requestIn(0, 1); component.doDispatch();
    ASSERT_FALSE(component.m_captureWindow.active());
    ASSERT_EQ(component.m_pendingPath, old);
    acknowledge(old);
    ASSERT_from_statusOut_SIZE(1);
    ASSERT_EQ(fromPortHistory_statusOut->at(0).productBytes, 0U);
    ASSERT_EQ(component.m_state, PayloadState::READY);
    ASSERT_from_cancelDoneOut_SIZE(1);
    clearHistory(); startShortCapture();
    ASSERT_NE(component.m_pendingPath, old);
    acknowledge(component.m_pendingPath);
    ASSERT_from_statusOut_SIZE(1); ASSERT_GT(fromPortHistory_statusOut->at(0).productBytes, 38400U);
    ASSERT_from_cancelDoneOut_SIZE(0);
}
void PayloadDriver_LeptonTester::cancelledWriteTimeoutRejectsLateResult() {
    startShortCapture(); const auto old = component.m_pendingPath;
    clearHistory(); invoke_to_cancelIn(0); component.doDispatch();
    for (unsigned i = 0; i < 10; ++i) dispatchRun();
    ASSERT_from_statusOut_SIZE(1); ASSERT_EQ(component.m_state, PayloadState::READY);
    ASSERT_from_cancelDoneOut_SIZE(1);
    clearHistory(); startShortCapture(); const auto fresh = component.m_pendingPath;
    ASSERT_NE(fresh, old);
    invoke_to_dpWrittenIn(0, Fw::String(old.c_str()), 10, 5); component.doDispatch();
    ASSERT_EQ(component.m_pendingPath, fresh); ASSERT_EQ(component.m_pendingWrites, 1U);
    ASSERT_from_statusOut_SIZE(0);
    acknowledge(fresh); ASSERT_from_statusOut_SIZE(1);
    ASSERT_GT(fromPortHistory_statusOut->at(0).productBytes, 38400U);
    ASSERT_from_cancelDoneOut_SIZE(0);
    // Stable/backward mission clocks still allocate distinct writer identities.
    Fw::Time time(0, 0); ASSERT_TRUE(component.assignWriteTime(time)); const auto first = time;
    time.set(0, 0); ASSERT_TRUE(component.assignWriteTime(time)); ASSERT_GT(time, first);
}
void PayloadDriver_LeptonTester::completionRacingCancel() {
    invoke_to_requestIn(0, 0); component.doDispatch();
    ASSERT_from_statusOut_SIZE(1); ASSERT_from_cancelDoneOut_SIZE(0); clearHistory();
    startShortCapture(); acknowledge(component.m_pendingPath); clearHistory();
    invoke_to_cancelIn(0); component.doDispatch();
    ASSERT_from_statusOut_SIZE(1); ASSERT_EQ(fromPortHistory_statusOut->at(0).productBytes, 0U);
    ASSERT_EQ(component.m_state, PayloadState::READY);
    ASSERT_from_cancelDoneOut_SIZE(1);
    clearHistory(); startShortCapture(); acknowledge(component.m_pendingPath);
    ASSERT_from_statusOut_SIZE(1); ASSERT_GT(fromPortHistory_statusOut->at(0).productBytes, 38400U);
    ASSERT_from_cancelDoneOut_SIZE(0);
}
}
