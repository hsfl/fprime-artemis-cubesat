// ======================================================================
// \title  ImuManager.hpp
// \brief  hpp file for ImuManager component implementation class
// ======================================================================

#ifndef Components_ImuManager_HPP
#define Components_ImuManager_HPP

#include "FprimeArtemisCore/Components/ImuManager/ImuManagerComponentAc.hpp"

namespace Components {

class ImuManager final : public ImuManagerComponentBase {
  public:
    //! Consecutive failed reads before ON becomes FAULT.
    //! At the 1Hz rate group this is 3 seconds.
    static constexpr U32 FAULT_THRESHOLD = 3;

    explicit ImuManager(const char* const compName);
    ~ImuManager();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! First tick: power the IMU on. Then read while ON and detect faults.
    void run_handler(FwIndexType portNum, U32 context) override;

    //! MissionApp requests the IMU on or off
    Fw::Success powerRequestIn_handler(FwIndexType portNum, const Fw::On& state) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Turn the IMU on or off
    void SET_IMU_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::On& state) override;

    //! Report the current state as an event
    void GET_IMU_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Ask the driver for a power state and update state. Shared by the port,
    //! the command, and the boot power-on.
    //! \return SUCCESS if the driver carried out the request
    Fw::Success applyPower(const Fw::On& state);

    //! Read one sample and publish it, or count the failure
    void readOnce();

    //! Set the state, emitting an event and telemetry if it changed
    void setState(ImuState state);

    //! Current state. OFF only until the first run tick powers the IMU on.
    ImuState m_state = ImuState::OFF;

    //! Whether the boot power-on has been requested
    bool m_bootPowerOnDone = false;

    //! Failed reads in a row while ON
    U32 m_consecutiveErrors = 0;

    //! Failed reads since boot
    U32 m_readErrors = 0;
};

}  // namespace Components

#endif
