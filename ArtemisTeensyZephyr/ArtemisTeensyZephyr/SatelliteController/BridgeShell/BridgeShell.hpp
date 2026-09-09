#ifndef SATELLITECONTROLLER_BRIDGESHELL_HPP
#define SATELLITECONTROLLER_BRIDGESHELL_HPP

#include "ArtemisTeensyZephyr/SatelliteController/BridgeShell/BridgeShellComponentAc.hpp"

namespace SatelliteController {

class BridgeShell final : public BridgeShellComponentBase {
  public:
    explicit BridgeShell(const char* const compName);
    ~BridgeShell() override;

  private:
    void run_handler(FwIndexType portNum, U32 context) override;
    void publishRadioState();

    U32 m_tickCount;
    U32 m_uartFramesReceived;
    U32 m_radioFramesDropped;
};

}  // namespace SatelliteController

#endif
