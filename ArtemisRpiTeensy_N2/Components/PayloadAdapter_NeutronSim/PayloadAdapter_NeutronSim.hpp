#ifndef Components_PayloadAdapter_NeutronSim_HPP
#define Components_PayloadAdapter_NeutronSim_HPP

#include "Components/PayloadAdapter_NeutronSim/PayloadAdapter_NeutronSimComponentAc.hpp"

#include <string>

namespace Components {

class PayloadAdapter_NeutronSim final : public PayloadAdapter_NeutronSimComponentBase {
  public:
    PayloadAdapter_NeutronSim(const char* const compName);
    ~PayloadAdapter_NeutronSim();

  private:
    struct CaptureSummary {
        U32 rows;
        U32 totalCounts;
        U32 saaRows;
        U32 productBytes;
        U32 exitStatus;
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 durationSeconds) override;

    CaptureSummary runCapture(U32 durationSeconds) const;
    static std::string getSimRoot();
    static std::string shellQuote(const std::string& value);
    static void parseSummaryLine(const std::string& line, CaptureSummary& summary);
    static U32 parseU32(const std::string& value);

    U32 m_lastDurationSeconds;
    U32 m_lastRowsCaptured;
    U32 m_lastTotalCounts;
    U32 m_lastSaaRows;
    U32 m_lastProductBytes;
    U32 m_lastExitStatus;
};

}  // namespace Components

#endif
