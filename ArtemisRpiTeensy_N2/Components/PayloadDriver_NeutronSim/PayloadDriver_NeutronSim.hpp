#ifndef Components_PayloadDriver_NeutronSim_HPP
#define Components_PayloadDriver_NeutronSim_HPP

#include "Components/PayloadDriver_NeutronSim/PayloadDriver_NeutronSimComponentAc.hpp"

#include <string>

namespace Components {

class PayloadDriver_NeutronSim final : public PayloadDriver_NeutronSimComponentBase {
  public:
    PayloadDriver_NeutronSim(const char* const compName);
    ~PayloadDriver_NeutronSim();

  private:
    struct CaptureSummary {
        U32 rows;
        U32 totalCounts;
        U32 saaRows;
        U32 productBytes;
        U32 exitStatus;
        std::string outputPath;
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 durationSeconds) override;

    CaptureSummary runCapture(U32 durationSeconds) const;
    static std::string getSimRoot();
    static std::string shellQuote(const std::string& value);
    static void parseSummaryLine(const std::string& line, CaptureSummary& summary);
    static bool publishLatestCapture(const std::string& outputPath);
    static bool computeFileCrc16(const std::string& outputPath, U32& crcOut);
    static U32 parseU32(const std::string& value);

    U32 m_lastDurationSeconds;
    U32 m_lastRowsCaptured;
    U32 m_lastTotalCounts;
    U32 m_lastSaaRows;
    U32 m_lastProductBytes;
    U32 m_lastExitStatus;
    U32 m_lastProductId;
    U32 m_lastProductCrc;
};

}  // namespace Components

#endif
