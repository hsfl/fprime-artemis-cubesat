#ifndef Components_LinkCfg_PayloadPaths_HPP
#define Components_LinkCfg_PayloadPaths_HPP

#include <cstdlib>
#include <string>

namespace Components {
namespace LinkCfg {

inline std::string environmentOrDefault(const char* const name, const char* const fallback) {
    const char* const value = std::getenv(name);
    return ((value != nullptr) && (value[0] != '\0')) ? std::string(value) : std::string(fallback);
}

inline std::string payloadCaptureDir() {
    return environmentOrDefault("NEUTRON_PAYLOAD_CAPTURE_DIR", "/tmp/neutron_payload_captures");
}

inline std::string payloadLatestPath() {
    return payloadCaptureDir() + "/latest_payload.bin";
}

inline std::string payloadSimCursorPath() {
    return environmentOrDefault("NEUTRON_PAYLOAD_SIM_CURSOR", "/tmp/neutron_payload_sim_cursor.json");
}

}  // namespace LinkCfg
}  // namespace Components

#endif
