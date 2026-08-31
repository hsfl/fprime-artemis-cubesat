#include "SatelliteController/ZephyrWatchdog.hpp"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>

namespace SatelliteController {

ZephyrWatchdog::ZephyrWatchdog() : m_device(DEVICE_DT_GET(DT_NODELABEL(wdog0))) {}

bool ZephyrWatchdog::initialize(std::uint32_t timeoutMs) {
    std::uint32_t resetCause = 0;
    if (hwinfo_get_reset_cause(&resetCause) == 0) {
        m_watchdogReset = (resetCause & RESET_WATCHDOG) != 0U;
        (void)hwinfo_clear_reset_cause();
    }
    if (m_device == nullptr || !device_is_ready(m_device)) return false;
    wdt_timeout_cfg config{};
    config.window.min = 0;
    config.window.max = timeoutMs;
    config.callback = nullptr;
    config.flags = WDT_FLAG_RESET_SOC;
    m_channel = wdt_install_timeout(m_device, &config);
    if (m_channel < 0) return false;
    return wdt_setup(m_device, 0) == 0;
}

bool ZephyrWatchdog::feed() {
    return m_channel >= 0 && wdt_feed(m_device, m_channel) == 0;
}

}  // namespace SatelliteController
