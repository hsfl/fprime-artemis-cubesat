// ======================================================================
// \title  GpsManager.hpp
// \brief  hpp file for GpsManager component implementation class
// ======================================================================

#ifndef Components_GpsManager_HPP
#define Components_GpsManager_HPP

#include "FprimeArtemisCore/Components/GpsManager/GpsManagerComponentAc.hpp"

namespace Components {

class GpsManager final : public GpsManagerComponentBase {
  public:
    explicit GpsManager(const char* const compName);
    ~GpsManager();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! First tick: wake the GPS. Then read the driver, update the state, and
    //! publish the fix.
    void run_handler(FwIndexType portNum, U32 context) override;

    //! MissionApp requests the GPS on or off
    Fw::Success powerRequestIn_handler(FwIndexType portNum, const Fw::On& state) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Turn the GPS on or off
    void SET_GPS_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::On& state) override;

    //! Report the current state as an event
    void GET_GPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Send a power request to the driver and record the commanded state.
    //! Shared by the port, the command, and the boot power-on.
    //! \return SUCCESS if the driver sent the request
    Fw::Success applyPower(const Fw::On& state);

    //! Set the state, emitting the ready/not-ready events on a change
    void setState(GpsState state);

    //! Current state. Nothing has been heard from the module at boot.
    GpsState m_state = GpsState::OFF;

    //! Commanded power state. OFF only until the first run tick wakes the GPS.
    Fw::On m_power = Fw::On::OFF;

    //! Whether the boot power-on has been requested
    bool m_bootPowerOnDone = false;

    //! Last valid fix. Held after a loss so the last known position stays
    //! readable, which is why it is published only while READY.
    GpsFix m_fix;

    //! Whether m_fix has ever been filled
    bool m_hasEverFixed = false;

    //! READY -> not READY transitions since boot
    U32 m_fixLostCount = 0;
};

}  // namespace Components

#endif
