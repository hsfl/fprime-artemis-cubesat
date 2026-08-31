#pragma once

#include <cstdint>

#include <zephyr/device.h>

namespace SatelliteController {

class ZephyrWatchdog final {
  public:
    ZephyrWatchdog();
    bool initialize(std::uint32_t timeoutMs);
    bool feed();
    bool watchdogReset() const { return m_watchdogReset; }

  private:
    const device* m_device;
    int m_channel = -1;
    bool m_watchdogReset = false;
};

}  // namespace SatelliteController
