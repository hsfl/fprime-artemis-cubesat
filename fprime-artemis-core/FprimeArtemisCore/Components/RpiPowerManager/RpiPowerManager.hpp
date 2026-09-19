// ======================================================================
// \title  RpiPowerManager.hpp
// \brief  hpp file for RpiPowerManager component implementation class
// ======================================================================

#ifndef Components_RpiPowerManager_HPP
#define Components_RpiPowerManager_HPP

#include "FprimeArtemisCore/Components/RpiPowerManager/RpiPowerManagerComponentAc.hpp"

namespace Components {

class RpiPowerManager final : public RpiPowerManagerComponentBase {
  public:
    //! Ticks of run without a peer heartbeat before READY falls back to BOOT.
    //! At the 1Hz rate group this is 10 seconds.
    static constexpr U32 PEER_TIMEOUT_TICKS = 10;

    explicit RpiPowerManager(const char* const compName);
    ~RpiPowerManager();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Age the peer heartbeat and refresh telemetry
    void run_handler(FwIndexType portNum, U32 context) override;

    //! Payload computer reported in: promote BOOT to READY
    void peerAliveIn_handler(FwIndexType portNum, U32 key) override;

    //! MissionApp requests the rail on or off
    Fw::Success powerRequestIn_handler(FwIndexType portNum, const Fw::On& state) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Enable or disable the payload computer power rail
    void SET_RPI_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::On& state) override;

    //! Report the current state as an event
    void GET_RPI_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Drive the pin and update state. Shared by the port and the command.
    //! \return SUCCESS if the driver accepted the write
    Fw::Success applyPower(const Fw::On& state);

    //! Set the reported state, emitting an event, telemetry, and stateOut if it changed
    void setState(RpiPowerState state);

    //! Current reported state. The rail is off at construction.
    RpiPowerState m_state = RpiPowerState::OFF;

    //! run ticks since the last peer heartbeat
    U32 m_ticksSincePeer = 0;

    //! Times the rail has been commanded on since boot
    U32 m_powerCycles = 0;
};

}  // namespace Components

#endif
