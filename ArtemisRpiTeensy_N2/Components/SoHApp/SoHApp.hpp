#ifndef Components_SoHApp_HPP
#define Components_SoHApp_HPP

#include "Components/SoHApp/SoHAppComponentAc.hpp"
#include "Os/Mutex.hpp"

namespace Components {

class SoHApp final : public SoHAppComponentBase {
  public:
    SoHApp(const char* const compName);
    ~SoHApp();

  private:
    enum SoHStatusSlots {
        SLOT_EPS = 0,
        SLOT_PAYLOAD = 1,
        SLOT_ADCS = 2,
        SLOT_GPS = 3,
        SLOT_STORAGE = 4,
        SLOT_THERMAL = 5,
        SLOT_COMMS = 6,
        SLOT_TRANSPORT = 7,
        SLOT_COUNT = 8,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void statusIn_handler(FwIndexType portNum, const Components::HealthState& health, U32 detail) override;
    void EMIT_SOH_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    //! Periodic SOH reports on or off (C&T PUS 3 TC 5 / TC 6). Turning on sends one report at once.
    void SET_SOH_REPORTING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Fw::Enabled reporting) override;
    //! F′ calls this after a PRM_SET. Reports a SOH_PERIOD_SECONDS outside 1 to 3600 s.
    void parameterUpdated(FwPrmIdType id) override;

    //! One SOH report: every health channel plus the Snapshot event. Caller holds m_lock.
    void reportLocked();
    //! FAIL anywhere, then WARN anywhere, then UNKNOWN from a required slot, else OK. Caller holds m_lock.
    Components::HealthState overallHealth(U8 requiredSlots) const;
    //! Parameter reads, with the FPP defaults (10 s, 0x3F) until the parameter database loads.
    U32 reportPeriodSeconds();
    U8 requiredSlots();

    // statusIn arrives on each manager's thread and run on the rate-group thread, while the
    // commands run on this component's own thread. Anything that touches the table, the timer
    // or the SOH channels holds m_lock, so two reports never overlap and the last values the
    // ground receives are always the newest.
    Os::Mutex m_lock;
    Components::HealthState m_status[SLOT_COUNT];
    U32 m_detail[SLOT_COUNT];
    bool m_reporting;          //!< periodic reports on (SET_SOH_REPORTING). Off at start-up.
    U32 m_ticksSinceReport;    //!< 1 Hz run ticks since the last periodic report
};

}  // namespace Components

#endif
