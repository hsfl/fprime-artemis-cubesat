#include "UHFRadioDriver_Sim.hpp"
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
K_SEM_DEFINE(twinRadioWake, 0, 1);
void rhWake() { k_sem_give(&twinRadioWake); }
void rhWait() { (void)k_sem_take(&twinRadioWake, K_MSEC(10)); }
namespace {
U16 crc16(const U8* bytes, size_t length) {
    U16 crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<U16>(bytes[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<U16>((crc << 1) ^ 0x1021) : static_cast<U16>(crc << 1);
    }
    return crc;
}
}
namespace Components {
UHFRadioDriver_Sim::UHFRadioDriver_Sim(const char* name) : UHFRadioDriver_SimComponentBase(name) {}
void UHFRadioDriver_Sim::configure() {
    device = DEVICE_DT_GET(DT_ALIAS(radio_uart));
    FW_ASSERT(device_is_ready(device));
    used = 0;
}
bool UHFRadioDriver_Sim::power_handler(FwIndexType, bool enabled) {
    state = enabled ? RadioState::RX : RadioState::OFF;
    return true;
}
bool UHFRadioDriver_Sim::send_handler(FwIndexType, const RadioPacket& packet) {
    const U8 length = packet.get_length();
    if (state != RadioState::RX || !length || length > 49) return false;
    U8 record[58] = {0x52, 0x46, length, packet.get_to(), packet.get_from(), packet.get_network(), packet.get_version()};
    for (unsigned i = 0; i < length; ++i) record[7 + i] = packet.get_bytes()[i];
    const U16 crc = crc16(record + 2, length + 5);
    record[7 + length] = static_cast<U8>(crc);
    record[8 + length] = static_cast<U8>(crc >> 8);
    for (unsigned i = 0; i < length + 9U; ++i) uart_poll_out(device, record[i]);
    return true;
}
bool UHFRadioDriver_Sim::receive_handler(FwIndexType, RadioPacket& packet) {
    if (state != RadioState::RX) return false;
    U8 byte;
    for (unsigned budget = 0; budget < 512 && uart_poll_in(device, &byte) == 0; ++budget) {
        if (used == 0 && byte != 0x52) continue;
        if (used == 1 && byte != 0x46) { used = byte == 0x52 ? 1 : 0; continue; }
        input[used++] = byte;
        if (used >= 3 && (input[2] == 0 || input[2] > 49)) { used = 0; ++crcDrops; continue; }
        if (used >= 3 && used == static_cast<size_t>(input[2]) + 9) {
            const U8 n = input[2];
            const U16 expected = static_cast<U16>(input[7 + n]) | (static_cast<U16>(input[8 + n]) << 8);
            used = 0;
            if (crc16(input + 2, n + 5) != expected) { ++crcDrops; continue; }
            RadioBytes data;
            for (unsigned i = 0; i < n; ++i) data[i] = input[7 + i];
            packet = RadioPacket(data, n, input[3], input[4], input[5], input[6]);
            return true;
        }
    }
    return false;
}
RadioStatus UHFRadioDriver_Sim::status_handler(FwIndexType) { return RadioStatus(state, -50, crcDrops); }
}
