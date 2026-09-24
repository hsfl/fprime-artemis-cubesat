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

    //! Read the driver, update the state, and publish the fix
    void run_handler(FwIndexType portNum, U32 context) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Report the current state as an event
    void GET_GPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Set the state, emitting the ready/not-ready events on a change
    void setState(GpsState state);

    //! Current state. Nothing has been heard from the module at boot.
    GpsState m_state = GpsState::OFF;

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
