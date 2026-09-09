#include "ArtemisTeensyZephyr/SatelliteController/BridgeShell/BridgeShell.hpp"
#include "SatelliteController/ControllerApp.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace SatelliteController {

BridgeShell::BridgeShell(const char* const compName)
    : BridgeShellComponentBase(compName),
      m_tickCount(0),
      m_uartFramesReceived(0),
      m_radioFramesDropped(0) {
    publishRadioState();
}

BridgeShell::~BridgeShell() = default;

void BridgeShell::run_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;
    controllerAppPoll(static_cast<std::uint32_t>(k_uptime_get_32()));
    const ControllerApp& app = controllerApp();
    m_uartFramesReceived = app.uartFramesReceived();
    const RuntimeBridge& bridge = app.bridge();
    m_radioFramesDropped = bridge.reassemblyDrops() + bridge.framingDrops() +
                           bridge.queueDrops() + app.radioStatus().rfTxDrops +
                           app.wrongNetworkDrops() + app.wrongAddressDrops() +
                           app.wrongVersionDrops();
    ++m_tickCount;
    this->tlmWrite_TickCount(m_tickCount);
    publishRadioState();
    if ((m_tickCount % 10U) == 0U) {
        printk("satellite bridge ticks=%u\n", static_cast<unsigned int>(m_tickCount));
    }
}

void BridgeShell::publishRadioState() {
    const RfStatusSnapshot snapshot = controllerApp().radioStatus();
    tlmWrite_UartFramesReceived(m_uartFramesReceived);
    tlmWrite_RadioFramesDropped(m_radioFramesDropped);
    tlmWrite_RadioState(static_cast<U32>(snapshot.state));
    tlmWrite_RadioFault(static_cast<U32>(snapshot.fault));
    tlmWrite_RadioInitAttempts(snapshot.initAttempts);
}

}  // namespace SatelliteController
