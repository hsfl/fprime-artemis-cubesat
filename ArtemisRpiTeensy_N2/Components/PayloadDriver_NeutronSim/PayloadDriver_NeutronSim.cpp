#include "Components/PayloadDriver_NeutronSim/PayloadDriver_NeutronSim.hpp"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sys/wait.h>
#include <unistd.h>

namespace Components {

namespace {

constexpr const char* DEFAULT_SIM_ROOT = "../external/payload-neutron-simulation";
constexpr const char* REPO_ROOT_SIM_ROOT = "external/payload-neutron-simulation";
constexpr const char* DEFAULT_CURSOR = "/tmp/neutron_payload_sim_cursor.json";
constexpr const char* DEFAULT_OUTPUT_DIR = "/tmp/neutron_payload_captures";
constexpr const char* DEFAULT_LATEST_PAYLOAD = "/tmp/neutron_payload_captures/latest_payload.bin";
constexpr U32 DEFAULT_CAPTURE_SECONDS = 600;

}  // namespace

PayloadDriver_NeutronSim::PayloadDriver_NeutronSim(const char* const compName)
    : PayloadDriver_NeutronSimComponentBase(compName),
      m_lastDurationSeconds(0),
      m_lastRowsCaptured(0),
      m_lastTotalCounts(0),
      m_lastSaaRows(0),
      m_lastProductBytes(0),
      m_lastExitStatus(0),
      m_lastProductId(0),
      m_lastProductCrc(0) {}

PayloadDriver_NeutronSim::~PayloadDriver_NeutronSim() {}

void PayloadDriver_NeutronSim::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadDriver_NeutronSim::requestIn_handler(FwIndexType portNum, U32 durationSeconds) {
    static_cast<void>(portNum);
    if (durationSeconds == 0U) {
        durationSeconds = DEFAULT_CAPTURE_SECONDS;
    }

    CaptureSummary summary = this->runCapture(durationSeconds);
    if ((summary.exitStatus == 0U) && !summary.outputPath.empty() && !publishLatestCapture(summary.outputPath)) {
        summary.exitStatus = 126U;
    }
    U32 sourceCrc = 0;
    if ((summary.exitStatus == 0U) && !computeFileCrc16(summary.outputPath, sourceCrc)) {
        summary.exitStatus = 125U;
    }
    const U32 productId = this->m_lastProductId + 1U;

    this->m_lastDurationSeconds = durationSeconds;
    this->m_lastRowsCaptured = summary.rows;
    this->m_lastTotalCounts = summary.totalCounts;
    this->m_lastSaaRows = summary.saaRows;
    this->m_lastProductBytes = summary.productBytes;
    this->m_lastExitStatus = summary.exitStatus;
    this->m_lastProductId = (summary.exitStatus == 0U) ? productId : this->m_lastProductId;
    this->m_lastProductCrc = sourceCrc;

    this->tlmWrite_LastDurationSeconds(this->m_lastDurationSeconds);
    this->tlmWrite_LastRowsCaptured(this->m_lastRowsCaptured);
    this->tlmWrite_LastTotalCounts(this->m_lastTotalCounts);
    this->tlmWrite_LastSaaRows(this->m_lastSaaRows);
    this->tlmWrite_LastProductBytes(this->m_lastProductBytes);
    this->tlmWrite_LastExitStatus(this->m_lastExitStatus);
    this->tlmWrite_LastProductId(this->m_lastProductId);
    this->tlmWrite_LastProductCrc(this->m_lastProductCrc);

    if (summary.exitStatus == 0U) {
        this->log_ACTIVITY_HI_CaptureComplete(durationSeconds, summary.rows, summary.productBytes, productId);
        if (this->isConnected_statusOut_OutputPort(0)) {
            const Fw::String sourcePath(summary.outputPath.c_str());
            this->statusOut_out(
                0, productId, summary.productBytes, Components::ScienceProductSource::NEUTRON_SIM, sourcePath, sourceCrc);
        }
    } else {
        this->log_WARNING_HI_CaptureFailed(durationSeconds, summary.exitStatus);
        if (this->isConnected_statusOut_OutputPort(0)) {
            const Fw::String emptyPath("");
            this->statusOut_out(0, 0U, 0U, Components::ScienceProductSource::UNKNOWN, emptyPath, 0U);
        }
    }
}

PayloadDriver_NeutronSim::CaptureSummary PayloadDriver_NeutronSim::runCapture(U32 durationSeconds) const {
    CaptureSummary summary = {};
    const std::string root = getSimRoot();
    const std::string script = root + "/neutron_payload_sim.py";
    const std::string dataset = root + "/neutron_data.csv";
    const std::string command = "python3 " + shellQuote(script) +
                                " capture --duration-seconds " + std::to_string(durationSeconds) +
                                " --dataset " + shellQuote(dataset) +
                                " --cursor " + shellQuote(DEFAULT_CURSOR) +
                                " --output-dir " + shellQuote(DEFAULT_OUTPUT_DIR) +
                                " --end-policy wrap --format kv 2>&1";

    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        summary.exitStatus = 127;
        return summary;
    }

    char buffer[256];
    while (::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        parseSummaryLine(buffer, summary);
    }

    const int status = ::pclose(pipe);
    if (status == 0) {
        summary.exitStatus = 0U;
    } else if (WIFEXITED(status)) {
        summary.exitStatus = static_cast<U32>(WEXITSTATUS(status));
    } else {
        summary.exitStatus = static_cast<U32>(status);
    }
    return summary;
}

std::string PayloadDriver_NeutronSim::getSimRoot() {
    const char* root = std::getenv("NEUTRON_PAYLOAD_SIM_ROOT");
    if ((root == nullptr) || (root[0] == '\0')) {
        const std::string defaultDataset = std::string(DEFAULT_SIM_ROOT) + "/neutron_data.csv";
        if (::access(defaultDataset.c_str(), R_OK) == 0) {
            return DEFAULT_SIM_ROOT;
        }
        const std::string repoRootDataset = std::string(REPO_ROOT_SIM_ROOT) + "/neutron_data.csv";
        if (::access(repoRootDataset.c_str(), R_OK) == 0) {
            return REPO_ROOT_SIM_ROOT;
        }
        return DEFAULT_SIM_ROOT;
    }
    return root;
}

std::string PayloadDriver_NeutronSim::shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

void PayloadDriver_NeutronSim::parseSummaryLine(const std::string& line, CaptureSummary& summary) {
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
        return;
    }
    const std::string key = line.substr(0, separator);
    std::string value = line.substr(separator + 1);
    while (!value.empty() &&
           ((value.back() == '\n') || (value.back() == '\r') || (value.back() == ' ') || (value.back() == '\t'))) {
        value.pop_back();
    }

    if (key == "rows") {
        summary.rows = parseU32(value);
    } else if (key == "total_counts") {
        summary.totalCounts = parseU32(value);
    } else if (key == "saa_rows") {
        summary.saaRows = parseU32(value);
    } else if (key == "output_bytes") {
        summary.productBytes = parseU32(value);
    } else if (key == "output_path") {
        summary.outputPath = value;
    }
}

bool PayloadDriver_NeutronSim::publishLatestCapture(const std::string& outputPath) {
    if (::access(outputPath.c_str(), R_OK) != 0) {
        return false;
    }
    (void)::unlink(DEFAULT_LATEST_PAYLOAD);
    return (::symlink(outputPath.c_str(), DEFAULT_LATEST_PAYLOAD) == 0);
}

bool PayloadDriver_NeutronSim::computeFileCrc16(const std::string& outputPath, U32& crcOut) {
    FILE* file = std::fopen(outputPath.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    U16 crc = 0xFFFFU;
    int value = 0;
    while ((value = std::fgetc(file)) != EOF) {
        crc ^= static_cast<U16>(static_cast<U8>(value)) << 8U;
        for (U8 bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0) {
                crc = static_cast<U16>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<U16>(crc << 1U);
            }
        }
    }
    (void)std::fclose(file);
    crcOut = static_cast<U32>(crc);
    return true;
}

U32 PayloadDriver_NeutronSim::parseU32(const std::string& value) {
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    static_cast<void>(end);
    if (parsed > std::numeric_limits<U32>::max()) {
        return std::numeric_limits<U32>::max();
    }
    return static_cast<U32>(parsed);
}

}  // namespace Components
