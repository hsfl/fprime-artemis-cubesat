#include "PayloadLinkServiceTester.hpp"
#include <cstdlib>
#include <fstream>
#include <dirent.h>
#include <unistd.h>

namespace Components {
std::string PayloadLinkServiceTester::createDirectory(const std::string& existing) {
    char name[] = "/tmp/artemis-pi-service-XXXXXX";
    const char* created = existing.empty() ? mkdtemp(name) : existing.c_str();
    FW_ASSERT(created != nullptr);
    const std::string result(created);
    setenv("ARTEMIS_PAYLOAD_CATALOG", (result + "/index").c_str(), 1);
    setenv("ARTEMIS_PAYLOAD_PRODUCT_DIR", result.c_str(), 1);
    return result;
}
PayloadLinkServiceTester::PayloadLinkServiceTester(const std::string& root)
    : PayloadLinkServiceGTestBase("Tester", MAX_HISTORY_SIZE), directory(createDirectory(root)), owner(root.empty()), component("Service") {
    initComponents(); connectPorts();
}
PayloadLinkServiceTester::~PayloadLinkServiceTester() {
    if (owner) removeProducts();
    unsetenv("ARTEMIS_PAYLOAD_CATALOG");
    unsetenv("ARTEMIS_PAYLOAD_PRODUCT_DIR");
}
void PayloadLinkServiceTester::removeProducts() {
    // Test-owned mkdtemp only. Production never enumerates product directories.
    const std::string records = directory + "/index.records";
    for (const auto& folder : {records, directory}) {
        DIR* dir = opendir(folder.c_str());
        if (!dir) continue;
        while (auto* entry = readdir(dir)) {
            const std::string name(entry->d_name);
            if (name != "." && name != "..") std::remove((folder + "/" + name).c_str());
        }
        closedir(dir);
        rmdir(folder.c_str());
    }
}
void PayloadLinkServiceTester::commit(U32 id, const std::string& name) {
    const std::string path = directory + "/" + name;
    std::ofstream file(path, std::ios::binary); file << "123456789"; file.close();
    invoke_to_productIn(0, id, 9, ScienceProductSource::REAL_PAYLOAD, Fw::String(path.c_str()), 0x29B1);
    clearHistory(); tick();
    ASSERT_from_messageOut_SIZE(1);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::PRODUCT_COMMITTED);
}
void PayloadLinkServiceTester::tick(unsigned count) {
    for (unsigned i = 0; i < count; ++i) invoke_to_run(0, 0);
}
TwinScience::Frame PayloadLinkServiceTester::request() {
    TwinScience::Frame frame;
    frame.set_operation(TwinScience::Operation::CAPTURE_REQUEST);
    frame.set_session(7); frame.set_requestId(1); frame.set_generation(1); frame.set_durationSeconds(3);
    return frame;
}
void PayloadLinkServiceTester::captureAdmissionAndDeduplication() {
    auto frame = request();
    invoke_to_messageIn(0, frame); tick();
    ASSERT_from_captureOut_SIZE(0);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::CAPTURE_REJECTED);
    invoke_to_stateIn(0, PayloadState::READY); clearHistory();
    invoke_to_messageIn(0, frame); tick();
    ASSERT_from_captureOut_SIZE(1); ASSERT_from_captureOut(0, 3);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::CAPTURE_ACCEPTED);
    clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_from_captureOut_SIZE(0);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::CAPTURE_ACCEPTED);
    clearHistory(); frame.set_durationSeconds(4); invoke_to_messageIn(0, frame); tick();
    ASSERT_from_captureOut_SIZE(0);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_error(), 3);  // Conflicting duplicate.
    frame = request(); frame.set_requestId(2); clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_error(), 1);  // Existing capture remains owner.
}
void PayloadLinkServiceTester::committedProductStagesOnlyOnMatchingAcks() {
    auto frame = request(); invoke_to_stateIn(0, PayloadState::READY); invoke_to_messageIn(0, frame); tick();
    const std::string path = directory + "/product.fdp";
    std::ofstream file(path, std::ios::binary); file << "123456789"; file.close();
    invoke_to_productIn(0, 42, 9, ScienceProductSource::REAL_PAYLOAD, Fw::String(path.c_str()), 0x29B1);
    clearHistory(); tick();
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::PRODUCT_COMMITTED);
    frame.set_operation(TwinScience::Operation::STAGE_REQUEST); frame.set_productId(42); frame.set_generation(5);
    clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::STAGE_BEGIN);
    frame.set_operation(TwinScience::Operation::STAGE_ACK); frame.set_generation(4); frame.set_offset(0);
    clearHistory(); invoke_to_messageIn(0, frame); tick(); ASSERT_from_messageOut_SIZE(0);  // Stale generation.
    frame.set_generation(5); invoke_to_messageIn(0, frame); tick();
    ASSERT_from_messageOut_SIZE(1);
    const auto& chunk = fromPortHistory_messageOut->at(0).frame;
    ASSERT_EQ(chunk.get_operation(), TwinScience::Operation::STAGE_CHUNK);
    ASSERT_EQ(chunk.get_length(), 9); ASSERT_EQ(chunk.get_data()[0], '1'); ASSERT_EQ(chunk.get_data()[8], '9');
    clearHistory(); frame.set_offset(9); invoke_to_messageIn(0, frame); tick();
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::STAGE_COMMIT);
    frame.set_operation(TwinScience::Operation::CACHE_READY); invoke_to_messageIn(0, frame); tick();
    clearHistory(); tick(250); ASSERT_from_messageOut_SIZE(0);  // No Pi reads/transmits after cache commitment.
    // Explicit restage after FC reset uses a new session without a new capture.
    frame.set_operation(TwinScience::Operation::STAGE_REQUEST); frame.set_session(8); frame.set_requestId(2); frame.set_generation(6);
    invoke_to_messageIn(0, frame); tick();
    ASSERT_from_captureOut_SIZE(0);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::STAGE_BEGIN);
}
void PayloadLinkServiceTester::stageTimeoutIsBounded() {
    auto frame = request(); invoke_to_stateIn(0, PayloadState::READY); invoke_to_messageIn(0, frame); tick();
    const std::string path = directory + "/product.fdp";
    std::ofstream file(path, std::ios::binary); file << "123456789"; file.close();
    invoke_to_productIn(0, 42, 9, ScienceProductSource::REAL_PAYLOAD, Fw::String(path.c_str()), 0x29B1); tick();
    frame.set_operation(TwinScience::Operation::STAGE_REQUEST); frame.set_productId(42); frame.set_generation(5);
    clearHistory(); invoke_to_messageIn(0, frame); tick(); tick(1000);
    ASSERT_from_messageOut_SIZE(6);  // Initial BEGIN, four retries, terminal ERROR.
    ASSERT_EQ(fromPortHistory_messageOut->at(5).frame.get_operation(), TwinScience::Operation::ERROR);
    ASSERT_EQ(fromPortHistory_messageOut->at(5).frame.get_error(), 7);
    clearHistory(); tick(500); ASSERT_from_messageOut_SIZE(0);
}
void PayloadLinkServiceTester::retainedProductsSurviveFailuresAndRestart() {
    auto frame = request(); invoke_to_stateIn(0, PayloadState::READY);
    invoke_to_messageIn(0, frame); tick(); commit(42, "a.fdp");
    frame.set_requestId(2); invoke_to_messageIn(0, frame); tick(); commit(1, "b.fdp");
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_productId(), 43);  // Persistent ID high-water.
    frame.set_requestId(3); invoke_to_messageIn(0, frame); tick();
    invoke_to_productIn(0, 0, 0, ScienceProductSource::REAL_PAYLOAD, Fw::String(""), 0); tick();
    frame.set_operation(TwinScience::Operation::STAGE_REQUEST); frame.set_requestId(4);
    frame.set_productId(42); frame.set_generation(8); clearHistory();
    invoke_to_messageIn(0, frame); tick();
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::STAGE_BEGIN);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_totalBytes(), 9);
    // A second actual component reads only persisted metadata, with a fresh FC session.
    PayloadLinkServiceTester restarted(directory);
    frame.set_session(99); frame.set_requestId(1); frame.set_productId(42);
    restarted.invoke_to_messageIn(0, frame); restarted.tick();
    ASSERT_EQ(restarted.fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::STAGE_BEGIN);
    frame.set_operation(TwinScience::Operation::ABORT);
    restarted.invoke_to_messageIn(0, frame); restarted.tick();
    frame.set_operation(TwinScience::Operation::CAPTURE_REQUEST); frame.set_requestId(2);
    restarted.invoke_to_stateIn(0, PayloadState::READY); restarted.invoke_to_messageIn(0, frame); restarted.tick();
    restarted.commit(1, "c.fdp");
    ASSERT_EQ(restarted.fromPortHistory_messageOut->at(0).frame.get_productId(), 44);
}
void PayloadLinkServiceTester::completedStageIsIdempotent() {
    auto frame = request(); invoke_to_stateIn(0, PayloadState::READY);
    invoke_to_messageIn(0, frame); tick(); commit(42, "product.fdp");
    frame.set_operation(TwinScience::Operation::STAGE_REQUEST); frame.set_productId(42); frame.set_generation(5);
    invoke_to_messageIn(0, frame); tick();
    frame.set_operation(TwinScience::Operation::STAGE_ACK); frame.set_offset(0);
    invoke_to_messageIn(0, frame); tick(); frame.set_offset(9); invoke_to_messageIn(0, frame); tick();
    frame.set_operation(TwinScience::Operation::CACHE_READY); invoke_to_messageIn(0, frame); tick();
    frame.set_operation(TwinScience::Operation::STAGE_REQUEST); clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_from_messageOut_SIZE(1);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::CACHE_READY);
    frame.set_generation(4); clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_error(), 4);  // Older stage generation.
    frame = request(); frame.set_requestId(2); clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_from_captureOut_SIZE(1);  // Duplicate final staging request did not occupy the file.
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::CAPTURE_ACCEPTED);
    // Once a newer capture owns the service, an older completed stage is stale.
    frame.set_operation(TwinScience::Operation::ABORT); invoke_to_messageIn(0, frame); tick();
    invoke_to_productIn(0, 0, 0, ScienceProductSource::REAL_PAYLOAD, Fw::String(""), 0);
    invoke_to_cancelDoneIn(0); tick();
    frame.set_operation(TwinScience::Operation::STAGE_REQUEST); frame.set_requestId(1);
    frame.set_productId(42); frame.set_generation(5); clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_error(), 4);
}
void PayloadLinkServiceTester::cancellationWaitsForDriverThenRecovers() {
    auto frame = request(); frame.set_durationSeconds(3600);
    invoke_to_stateIn(0, PayloadState::READY); invoke_to_messageIn(0, frame); tick();
    frame.set_operation(TwinScience::Operation::ABORT); clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_from_cancelOut_SIZE(1);
    frame.set_operation(TwinScience::Operation::CAPTURE_REQUEST); frame.set_requestId(2);
    clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_error(), 1);  // Do not reuse the outstanding driver callback.
    invoke_to_productIn(0, 0, 0, ScienceProductSource::REAL_PAYLOAD, Fw::String(""), 0);
    invoke_to_stateIn(0, PayloadState::READY); invoke_to_cancelDoneIn(0); tick();
    clearHistory(); invoke_to_messageIn(0, frame); tick();
    ASSERT_from_captureOut_SIZE(1);
    ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::CAPTURE_ACCEPTED);
    commit(1, "recovered.fdp");
}

void PayloadLinkServiceTester::pendingResultCannotReleaseCancellation() {
    // Exercise both an already-pending success and an already-pending failure.
    for (unsigned successful = 0; successful != 2; ++successful) {
        auto frame = request(); frame.set_requestId(1 + successful * 2);
        invoke_to_stateIn(0, PayloadState::READY); clearHistory();
        invoke_to_messageIn(0, frame); tick();
        ASSERT_from_captureOut_SIZE(1);
        invoke_to_cancelDoneIn(0);  // A spurious pre-cancellation signal is ignored.
        invoke_to_productIn(0, successful ? 42 : 0, successful ? 9 : 0,
                            ScienceProductSource::REAL_PAYLOAD, Fw::String("old.fdp"), 0);
        frame.set_operation(TwinScience::Operation::ABORT); clearHistory();
        invoke_to_messageIn(0, frame); tick();
        ASSERT_from_cancelOut_SIZE(1);
        frame.set_operation(TwinScience::Operation::CAPTURE_REQUEST); frame.set_requestId(2 + successful * 2);
        clearHistory(); invoke_to_messageIn(0, frame); tick();
        ASSERT_from_captureOut_SIZE(0);
        ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_error(), 1);
        // Neither an old positive result nor its generic zero failure is Done.
        invoke_to_productIn(0, 42, 9, ScienceProductSource::REAL_PAYLOAD, Fw::String("old.fdp"), 0);
        invoke_to_productIn(0, 0, 0, ScienceProductSource::UNKNOWN, Fw::String(""), 0);
        invoke_to_stateIn(0, PayloadState::READY); tick();
        clearHistory(); invoke_to_messageIn(0, frame); tick();
        ASSERT_from_captureOut_SIZE(0);
        ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_error(), 1);
        invoke_to_cancelDoneIn(0); tick();
        clearHistory(); invoke_to_messageIn(0, frame); tick();
        ASSERT_from_captureOut_SIZE(1);
        ASSERT_EQ(fromPortHistory_messageOut->at(0).frame.get_operation(), TwinScience::Operation::CAPTURE_ACCEPTED);
        commit(1, successful ? "clean-after-positive.fdp" : "clean-after-failure.fdp");
    }
}

}
