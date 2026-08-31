#pragma once

#include <cstdint>

namespace SatelliteController {

enum class RadioState : std::uint8_t { OFF = 0, READY = 1, INITIALIZING = 2, TX = 3, RECOVERING = 4 };
enum class RadioFault : std::uint8_t { NONE = 0, INIT_FAILED = 1, WATCHDOG_RESET = 2, LOCAL_TX = 3 };
enum class RadioAction : std::uint8_t { NONE, SAFE_OFF, START_INIT, ENTER_RX, START_TX, RECOVER_FIFO };

class RadioStateMachine final {
  public:
    RadioAction boot(bool watchdogReset);
    RadioAction setEnabled(bool enabled);
    RadioAction initComplete(bool success);
    RadioAction requestTx();
    RadioAction txComplete(bool success);
    RadioAction recoveryComplete(bool success);
    RadioState state() const { return m_state; }
    RadioFault fault() const { return m_fault; }
    std::uint32_t initAttempts() const { return m_initAttempts; }

  private:
    RadioState m_state = RadioState::OFF;
    RadioFault m_fault = RadioFault::NONE;
    std::uint32_t m_initAttempts = 0;
};

}  // namespace SatelliteController
