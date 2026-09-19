// ======================================================================
// \title  MissionApp.hpp
// \brief  hpp file for MissionApp component implementation class
// ======================================================================

#ifndef Components_MissionApp_HPP
#define Components_MissionApp_HPP

#include "FprimeArtemisCore/Components/MissionApp/MissionAppComponentAc.hpp"

namespace Components {

class MissionApp final : public MissionAppComponentBase {
  public:
    //! Seconds (1Hz ticks) to wait for the payload computer to report READY
    //! after power-on. Covers a Raspberry Pi Zero W Linux boot plus the
    //! deployment's systemd start.
    static constexpr U32 BASE_ENTRY_TIMEOUT_TICKS = 120;

    explicit MissionApp(const char* const compName);
    ~MissionApp();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Payload computer power state changed
    void rpiStateIn_handler(FwIndexType portNum, const Components::RpiPowerState& state) override;

    //! 1Hz tick: enforce STANDBY at boot, and time out base-mode entry
    void run_handler(FwIndexType portNum, U32 context) override;

    //! Health ping
    void pingIn_handler(FwIndexType portNum, U32 key) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    void ENTER_BASE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    void ENTER_STANDBY_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Set the mode, emitting an event and telemetry if it changed
    void setMode(MissionMode mode);

    //! Request payload computer power-off and enter STANDBY
    void goToStandby();

    //! Abandon base-mode entry: report why, power off, return to STANDBY
    void failBaseEntry(BaseModeFailure reason);

    MissionMode m_mode = MissionMode::STANDBY;

    //! Last payload computer state reported by RpiPowerManager. Accurate
    //! because every change is reported through rpiStateIn, and
    //! RpiPowerManager starts OFF.
    RpiPowerState m_rpiState = RpiPowerState::OFF;

    //! run ticks spent in ENTERING_BASE
    U32 m_entryTicks = 0;

    //! STANDBY is enforced on the first run tick, once drivers are configured
    bool m_standbyEnforced = false;
};

}  // namespace Components

#endif
