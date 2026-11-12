#ifndef ARTEMIS_LEPTON_CAPTURE_WINDOW_HPP
#define ARTEMIS_LEPTON_CAPTURE_WINDOW_HPP
#include <cstdint>
#include <limits>

namespace Components {
// A nonblocking collection interval. The driver keeps processing health pings
// while acquisition is BUSY and selects the newest frame when the interval ends.
class CaptureWindow {
  public:
    static constexpr std::uint32_t MAX_DURATION_SECONDS = 3600;
    bool start(std::uint64_t nowMs, std::uint32_t seconds) {
        const std::uint64_t interval = static_cast<std::uint64_t>(seconds) * 1000;
        if (m_active || seconds == 0 || seconds > MAX_DURATION_SECONDS ||
            nowMs > std::numeric_limits<std::uint64_t>::max() - interval) return false;
        m_endMs = nowMs + interval;
        m_active = true;
        return true;
    }
    bool elapsed(std::uint64_t nowMs) const { return m_active && nowMs >= m_endMs; }
    bool active() const { return m_active; }
    void finish() { m_active = false; }
  private:
    bool m_active = false;
    std::uint64_t m_endMs = 0;
};
}
#endif
