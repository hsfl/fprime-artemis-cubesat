#include "Components/SoHApp/SoHApp.hpp"

namespace Components {

namespace {
// Same as the FPP defaults. Used until the parameter database loads, or for a bad stored value.
constexpr U32 DEFAULT_REPORT_PERIOD_SECONDS = 10U;
constexpr U32 MAX_REPORT_PERIOD_SECONDS = 3600U;
constexpr U8 DEFAULT_REQUIRED_SLOTS = 0x3FU;

bool usable(Fw::ParamValid valid) {
    return (valid == Fw::ParamValid::VALID) || (valid == Fw::ParamValid::DEFAULT);
}
}  // namespace

SoHApp::SoHApp(const char* const compName)
    : SoHAppComponentBase(compName),
      m_status{},
      m_detail{},
      m_reporting(false),
      m_ticksSinceReport(0U) {
    for (U32 slot = 0; slot < SLOT_COUNT; ++slot) {
        this->m_status[slot] = Components::HealthState::UNKNOWN;
    }
}

SoHApp::~SoHApp() {}

void SoHApp::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

// The July 2026 RF-budget review switched this tick off because it wrote all nine channels
// every second. Now it sends nothing while reporting is off (the default), and one report
// every SOH_PERIOD_SECONDS while it is on.
void SoHApp::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    const U32 period = this->reportPeriodSeconds();
    Os::ScopeLock lock(this->m_lock);
    if (this->m_reporting) {
        this->m_ticksSinceReport += 1U;
        if (this->m_ticksSinceReport >= period) {
            this->m_ticksSinceReport = 0U;
            this->reportLocked();
        }
    }
}

// Each manager drops its latest health into its own slot (topology.fpp: 0 EPS, 1 payload, 2 ADCS, 3 GPS,
// 4 storage, 5 thermal, 6 comms, 7 transport). Stored only: it reaches the ground with the next report.
void SoHApp::statusIn_handler(FwIndexType portNum, const Components::HealthState& health, U32 detail) {
    if ((portNum >= 0) && (portNum < SLOT_COUNT)) {
        Os::ScopeLock lock(this->m_lock);
        this->m_status[portNum] = health;
        this->m_detail[portNum] = detail;
    }
}

// One report now, on demand (C&T PUS 3 TM 25 style).
void SoHApp::EMIT_SOH_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    {
        Os::ScopeLock lock(this->m_lock);
        this->reportLocked();
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// Periodic reports on or off (C&T PUS 3 TC 5 and TC 6). Off at start-up.
void SoHApp::SET_SOH_REPORTING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Fw::Enabled reporting) {
    if (!reporting.isValid()) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    const bool on = (reporting == Fw::Enabled::ENABLED);
    const U32 period = this->reportPeriodSeconds();
    {
        // Held through the report, so a tick cannot slip an old "reporting on" value in after this one.
        Os::ScopeLock lock(this->m_lock);
        this->m_reporting = on;
        this->m_ticksSinceReport = 0U;
        this->tlmWrite_SohReporting(reporting);
        this->log_ACTIVITY_HI_SohReportingChanged(reporting, period);
        // MVP step 2.1 "begin downlink of SOH data": the first report goes out now, not one period later.
        if (on) {
            this->reportLocked();
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// F′ calls this after a PRM_SET. A report period outside 1 to 3600 s is reported here and never used:
// reportPeriodSeconds() falls back to 10 s.
void SoHApp::parameterUpdated(FwPrmIdType id) {
    if (id == PARAMID_SOH_PERIOD_SECONDS) {
        Fw::ParamValid valid = Fw::ParamValid::INVALID;
        const U32 seconds = this->paramGet_SOH_PERIOD_SECONDS(valid);
        if (usable(valid) && ((seconds == 0U) || (seconds > MAX_REPORT_PERIOD_SECONDS))) {
            // The ground knows parameters by their full ID (component base + index).
            this->log_WARNING_LO_SohParameterRejected(
                static_cast<U32>(this->getIdBase() + PARAMID_SOH_PERIOD_SECONDS), seconds);
        }
    }
}

// Caller holds m_lock: one report at a time, built from the table as it is right now.
// None of these calls comes back into SoHApp, so holding the lock here cannot deadlock.
void SoHApp::reportLocked() {
    const Components::HealthState overall = this->overallHealth(this->requiredSlots());
    this->tlmWrite_OverallHealth(overall);
    this->tlmWrite_EpsHealth(this->m_status[SLOT_EPS]);
    this->tlmWrite_PayloadHealth(this->m_status[SLOT_PAYLOAD]);
    this->tlmWrite_AdcsHealth(this->m_status[SLOT_ADCS]);
    this->tlmWrite_GpsHealth(this->m_status[SLOT_GPS]);
    this->tlmWrite_StorageHealth(this->m_status[SLOT_STORAGE]);
    this->tlmWrite_ThermalHealth(this->m_status[SLOT_THERMAL]);
    this->tlmWrite_CommsHealth(this->m_status[SLOT_COMMS]);
    this->tlmWrite_TransportHealth(this->m_status[SLOT_TRANSPORT]);
    this->tlmWrite_SohReporting(this->m_reporting ? Fw::Enabled::ENABLED : Fw::Enabled::DISABLED);
    this->log_ACTIVITY_HI_Snapshot(
        overall,
        this->m_status[SLOT_EPS],
        this->m_status[SLOT_PAYLOAD],
        this->m_status[SLOT_ADCS],
        this->m_status[SLOT_GPS],
        this->m_status[SLOT_STORAGE],
        this->m_status[SLOT_THERMAL],
        this->m_status[SLOT_COMMS],
        this->m_status[SLOT_TRANSPORT]);
}

// FAIL anywhere wins, then WARN anywhere. A slot that never reported only makes the
// result UNKNOWN when this build needs it (SOH_REQUIRED_SLOTS): the CDR build has no
// CommsApp, so its comms slot stays UNKNOWN and used to hold OverallHealth there forever.
// Caller holds m_lock.
Components::HealthState SoHApp::overallHealth(U8 requiredSlots) const {
    bool sawWarn = false;
    bool sawUnknown = false;
    for (U32 slot = 0; slot < SLOT_COUNT; ++slot) {
        const Components::HealthState health = this->m_status[slot];
        if (health == Components::HealthState::FAIL) {
            return Components::HealthState::FAIL;
        }
        if (health == Components::HealthState::WARN) {
            sawWarn = true;
        }
        const bool required = ((static_cast<U32>(requiredSlots) >> slot) & 1U) != 0U;
        if ((health == Components::HealthState::UNKNOWN) && required) {
            sawUnknown = true;
        }
    }
    if (sawWarn) {
        return Components::HealthState::WARN;
    }
    if (sawUnknown) {
        return Components::HealthState::UNKNOWN;
    }
    return Components::HealthState::OK;
}

// Parameter reads, with the FPP defaults (10 s, 0x3F) as the fallback until the parameter database loads.
U32 SoHApp::reportPeriodSeconds() {
    Fw::ParamValid valid = Fw::ParamValid::INVALID;
    const U32 seconds = this->paramGet_SOH_PERIOD_SECONDS(valid);
    return (usable(valid) && (seconds >= 1U) && (seconds <= MAX_REPORT_PERIOD_SECONDS))
               ? seconds
               : DEFAULT_REPORT_PERIOD_SECONDS;
}

U8 SoHApp::requiredSlots() {
    Fw::ParamValid valid = Fw::ParamValid::INVALID;
    const U8 mask = this->paramGet_SOH_REQUIRED_SLOTS(valid);
    return usable(valid) ? mask : DEFAULT_REQUIRED_SLOTS;
}

}  // namespace Components
