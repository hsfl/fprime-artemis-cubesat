#pragma once
#include "FprimeArtemisCore/Components/UHFRadioDriver_Sim/UHFRadioDriver_SimComponentAc.hpp"
#include <zephyr/device.h>
#include <cstddef>
namespace Components {
class UHFRadioDriver_Sim final : public UHFRadioDriver_SimComponentBase {
  public:
    explicit UHFRadioDriver_Sim(const char* name);
    void configure();
  private:
    bool power_handler(FwIndexType, bool enabled) override;
    bool send_handler(FwIndexType, const RadioPacket&) override;
    bool receive_handler(FwIndexType, RadioPacket&) override;
    RadioStatus status_handler(FwIndexType) override;
    const struct device* device = nullptr;
    RadioState state = RadioState::OFF;
    U8 input[58] = {};
    size_t used = 0;
    U32 crcDrops = 0;
};
}
