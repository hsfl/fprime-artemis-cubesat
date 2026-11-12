#pragma once
// F Prime must precede Zephyr: Zephyr defines EMPTY, also an Os::Queue enumerator.
#include "FprimeArtemisCore/Components/UHFRadioManager/UHFRadioManagerComponentAc.hpp"
#include "LinkCore.hpp"

#include <zephyr/kernel.h>
#include <atomic>
namespace Components {
class UHFRadioManager : public UHFRadioManagerComponentBase, private RadioLink::Transport {
   public:
    explicit UHFRadioManager(const char* name);
    void startRadio(bool enabled);
    void stopRadio();

   private:
    void pingIn_handler(FwIndexType, U32) override;
    void dataIn_handler(FwIndexType, Fw::Buffer&, const ComCfg::FrameContext&) override;
    void dataReturnIn_handler(FwIndexType, Fw::Buffer&, const ComCfg::FrameContext&) override;
    bool powerRequestIn_handler(FwIndexType, bool) override;
    void RADIO_SET_ENABLED_cmdHandler(FwOpcodeType, U32, bool) override;
    void RADIO_SEND_TEST_PACKET_cmdHandler(FwOpcodeType, U32) override;
    void finishTest(FwOpcodeType, U32, bool);
    void RADIO_RESET_COUNTERS_cmdHandler(FwOpcodeType, U32) override;
    static void entry(void*, void*, void*);
    void work();
    void notify(Fw::Success status);
    uint32_t now() override;
    void pause(uint32_t) override;
    bool send(const RadioLink::Packet&) override;
    bool receive(RadioLink::Packet&) override;
    void frame(const uint8_t*, uint16_t) override;
    void frame(uint8_t channel, const uint8_t*, uint16_t) override;
    k_thread thread;
    k_mutex mutex;
    std::atomic<bool> stopping{false};
    bool started = false, pending = false, powerPending = false, requestedPower = false, resetPending = false;
    bool testPending = false, testOutstanding = false;
    FwOpcodeType testOpcode = 0;
    U32 testSequence = 0;
    bool pingPending = false;
    U32 pingKey = 0;
    Fw::Buffer txBuffer;
    ComCfg::FrameContext txContext;
    RadioLink::LinkCore link;
};
}  // namespace Components
