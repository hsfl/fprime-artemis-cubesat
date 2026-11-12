#include "UHFRadioManager.hpp"

#include <cstring>
#include "TestPacket.hpp"
void rhWake();
void rhWait();
K_THREAD_STACK_DEFINE(radioStack, 8192);
namespace Components {
UHFRadioManager::UHFRadioManager(const char* name) : UHFRadioManagerComponentBase(name), link(*this) {
    k_mutex_init(&mutex);
}
void UHFRadioManager::startRadio(bool enabled) {
    requestedPower = enabled;
    powerPending = true;
    stopping = false;
    started = true;
    k_thread_create(&thread, radioStack, K_THREAD_STACK_SIZEOF(radioStack), entry, this, nullptr, nullptr, 0, 0,
                    K_NO_WAIT);
    k_thread_name_set(&thread, "uhf-radio");
}
void UHFRadioManager::stopRadio() {
    if (started) {
        stopping = true;
        rhWake();
        k_thread_join(&thread, K_FOREVER);
        started = false;
    }
}
void UHFRadioManager::entry(void* self, void*, void*) {
    static_cast<UHFRadioManager*>(self)->work();
}
bool UHFRadioManager::powerRequestIn_handler(FwIndexType, bool enabled) {
    k_mutex_lock(&mutex, K_FOREVER);
    requestedPower = enabled;
    powerPending = true;
    k_mutex_unlock(&mutex);
    rhWake();
    return true;
}
void UHFRadioManager::RADIO_SET_ENABLED_cmdHandler(FwOpcodeType op, U32 seq, bool enabled) {
    powerRequestIn_handler(0, enabled);
    cmdResponse_out(op, seq, Fw::CmdResponse::OK);
}
void UHFRadioManager::RADIO_SEND_TEST_PACKET_cmdHandler(FwOpcodeType op, U32 seq) {
    k_mutex_lock(&mutex, K_FOREVER);
    const bool accepted = !testOutstanding && !stopping.load();
    if (accepted) {
        testOpcode = op;
        testSequence = seq;
        testPending = true;
        testOutstanding = true;
    }
    k_mutex_unlock(&mutex);
    if (accepted) rhWake();
    else cmdResponse_out(op, seq, Fw::CmdResponse::EXECUTION_ERROR);
    // Accepted requests respond only after the worker attempts transmission.
}
void UHFRadioManager::finishTest(FwOpcodeType op, U32 seq, bool success) {
    k_mutex_lock(&mutex, K_FOREVER);
    testOutstanding = false;
    k_mutex_unlock(&mutex);
    log_ACTIVITY_HI_RadioTestPacketResult(seq, success, sizeof(RadioLink::TEST_PACKET));
    cmdResponse_out(op, seq, success ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}
void UHFRadioManager::RADIO_RESET_COUNTERS_cmdHandler(FwOpcodeType op, U32 seq) {
    k_mutex_lock(&mutex, K_FOREVER);
    resetPending = true;
    k_mutex_unlock(&mutex);
    cmdResponse_out(op, seq, Fw::CmdResponse::OK);
}
void UHFRadioManager::pingIn_handler(FwIndexType, U32 key) {
    k_mutex_lock(&mutex, K_FOREVER);
    pingKey = key;
    pingPending = true;
    k_mutex_unlock(&mutex);
    rhWake();
}
void UHFRadioManager::dataIn_handler(FwIndexType, Fw::Buffer& buffer, const ComCfg::FrameContext& context) {
    k_mutex_lock(&mutex, K_FOREVER);
    bool accepted = !pending;
    if (accepted) {
        txBuffer = buffer;
        txContext = context;
        pending = true;
    }
    k_mutex_unlock(&mutex);
    rhWake();
    if (!accepted) {
        dataReturnOut_out(0, buffer, context);
        notify(Fw::Success::FAILURE);
    }
}
void UHFRadioManager::dataReturnIn_handler(FwIndexType, Fw::Buffer& buffer, const ComCfg::FrameContext&) {
    deallocate_out(0, buffer);
}
uint32_t UHFRadioManager::now() {
    return k_uptime_get_32();
}
void UHFRadioManager::pause(uint32_t ms) {
    k_msleep(ms);
}
bool UHFRadioManager::send(const RadioLink::Packet& p) {
    RadioBytes bytes;
    for (unsigned i = 0; i < p.length; ++i)
        bytes[i] = p.bytes[i];
    return driverSend_out(0, RadioPacket(bytes, p.length, p.to, p.from, p.network, p.version));
}
bool UHFRadioManager::receive(RadioLink::Packet& p) {
    RadioPacket packet;
    if (!driverReceive_out(0, packet))
        return false;
    p.length = packet.get_length();
    p.to = packet.get_to();
    p.from = packet.get_from();
    p.network = packet.get_network();
    p.version = packet.get_version();
    for (unsigned i = 0; i < p.length && i < 49; ++i)
        p.bytes[i] = packet.get_bytes()[i];
    return true;
}
void UHFRadioManager::frame(const uint8_t* bytes, uint16_t size) {
    if (!isConnected_dataOut_OutputPort(0)) {
        ++link.counters.drops;
        return;
    }
    Fw::Buffer buffer = allocate_out(0, size);
    if (!buffer.isValid() || buffer.getSize() < size) {
        if (buffer.isValid())
            deallocate_out(0, buffer);
        ++link.counters.drops;
        return;
    }
    std::memcpy(buffer.getData(), bytes, size);
    buffer.setSize(size);
    ComCfg::FrameContext context;
    dataOut_out(0, buffer, context);
}
void UHFRadioManager::frame(uint8_t channel, const uint8_t* bytes, uint16_t size) {
    if (channel == link_protocol::CHANNEL_CCSDS) {
        frame(bytes, size);
        return;
    }
    if (channel != link_protocol::CHANNEL_PAYLOAD || size > 44 ||
        !isConnected_scienceRetry_OutputPort(0)) {
        ++link.counters.drops;
        return;
    }
    ScienceBytes packet;
    for (U32 i = 0; i < size; ++i) packet[i] = bytes[i];
    scienceRetry_out(0, packet, static_cast<U8>(size));
}
void UHFRadioManager::notify(Fw::Success status) {
    comStatusOut_out(0, status);
}
void UHFRadioManager::work() {
    uint32_t lastTlm = 0, recoverAt = 0;
    bool enabled = false, desired = false, readySignalled = false;
    uint32_t retryPowerAt = 0;
    uint8_t retryBytes[link_protocol::FRAME_MAX_PAYLOAD];
    uint16_t retryLength = 0;
    while (!stopping) {
        k_mutex_lock(&mutex, K_FOREVER);
        bool change = powerPending;
        const bool requested = requestedPower, reset = resetPending;
        const bool doTest = testPending;
        const FwOpcodeType testOp = testOpcode;
        const U32 testSeq = testSequence;
        testPending = false;
        const bool ping = pingPending;
        const U32 key = pingKey;
        pingPending = false;
        powerPending = false;
        resetPending = false;
        Fw::Buffer buffer;
        ComCfg::FrameContext context;
        const bool haveTx = pending;
        if (haveTx) {
            buffer = txBuffer;
            context = txContext;
            pending = false;
        }
        k_mutex_unlock(&mutex);
        if (ping)
            pingOut_out(0, key);
        if (reset)
            link.counters = {};
        // Repeated requests for the current healthy state leave traffic untouched.
        if (change && requested == desired && enabled == requested)
            change = false;
        if (change) {
            desired = requested;
            recoverAt = 0;
            retryPowerAt = 0;
            retryLength = 0;
            link.reset();
            const bool ok = driverPower_out(0, requested);
            enabled = requested && ok;
            if (desired && !ok)
                retryPowerAt = now() + 5000;
            log_ACTIVITY_HI_RadioPowerChanged(requested, ok);
            // Return an existing downlink before notifying upstream of readiness.
            if (haveTx)
                dataReturnOut_out(0, buffer, context);
            if (isConnected_comStatusOut_OutputPort(0) && (!enabled || !readySignalled || haveTx)) {
                notify(enabled ? Fw::Success::SUCCESS : Fw::Success::FAILURE);
            }
            readySignalled = enabled;
        } else {
            if (enabled)
                link.poll();
            if (haveTx) {
                retryLength = buffer.isValid() && buffer.getSize() <= sizeof(retryBytes) ? buffer.getSize() : 0;
                if (retryLength)
                    std::memcpy(retryBytes, buffer.getData(), retryLength);
                const bool ok = enabled && buffer.isValid() && buffer.getSize() <= sizeof(retryBytes) &&
                                link.transmit(buffer.getData(), buffer.getSize());
                if (ok)
                    retryLength = 0;
                dataReturnOut_out(0, buffer, context);
                notify(ok ? Fw::Success::SUCCESS : Fw::Success::FAILURE);
                if (!ok) {
                    readySignalled = false;
                    if (enabled)
                        recoverAt = now() + 100;
                }
            }
        }
        // FAILURE pauses ComQueue. Recover explicitly after a failed transfer.
        if (enabled && recoverAt && int32_t(now() - recoverAt) >= 0) {
            recoverAt = 0;
            enabled = driverPower_out(0, true);
            link.reset();
            if (enabled && retryLength) {
                if (link.transmit(retryBytes, retryLength))
                    retryLength = 0;
                else {
                    enabled = false;
                    driverPower_out(0, false);
                }
            }
            if (enabled) {
                readySignalled = true;
                if (isConnected_comStatusOut_OutputPort(0))
                    notify(Fw::Success::SUCCESS);
            } else
                retryPowerAt = now() + 5000;
        }
        if (desired && !enabled && retryPowerAt && int32_t(now() - retryPowerAt) >= 0) {
            enabled = driverPower_out(0, true);
            retryPowerAt = enabled ? 0 : now() + 5000;
            if (enabled && retryLength) {
                if (link.transmit(retryBytes, retryLength))
                    retryLength = 0;
                else {
                    enabled = false;
                    driverPower_out(0, false);
                    retryPowerAt = now() + 5000;
                }
            }
            if (enabled) {
                log_ACTIVITY_HI_RadioPowerChanged(true, true);
                readySignalled = true;
                if (isConnected_comStatusOut_OutputPort(0))
                    notify(Fw::Success::SUCCESS);
            }
        }
        if (doTest) {
            // Never feed test completion into ComQueue's normal backpressure chain.
            const bool success = enabled && link.transmit(RadioLink::TEST_PACKET, sizeof(RadioLink::TEST_PACKET));
            finishTest(testOp, testSeq, success);
        }
        // Keep one hardware owner. Recheck control/health/power between bounded
        // science packets so a backlog cannot monopolize the radio worker.
        if (enabled && !recoverAt && isConnected_scienceNext_OutputPort(0) &&
            isConnected_scienceSent_OutputPort(0)) {
            for (unsigned i = 0; i < 4; ++i) {
                k_mutex_lock(&mutex, K_FOREVER);
                const bool urgent = pending || pingPending || powerPending || resetPending || testPending || stopping;
                k_mutex_unlock(&mutex);
                if (urgent) break;
                ScienceBytes bytes;
                U8 length = 0;
                if (!scienceNext_out(0, bytes, length)) break;
                if (length == 0 || length > 44) {
                    ++link.counters.drops;
                    scienceSent_out(0, false);
                    break;
                }
                uint8_t payload[44];
                for (unsigned n = 0; n < length; ++n) payload[n] = bytes[n];
                const bool ok = link.transmitChannel(link_protocol::CHANNEL_PAYLOAD, payload, length);
                scienceSent_out(0, ok);
                if (!ok) {
                    readySignalled = false;
                    recoverAt = now() + 100;
                    break;
                }
                link.poll();
                if (i + 1 < 4) pause(link_protocol::PAYLOAD_CACHE_RF_GAP_MS);
            }
        }
        if (uint32_t(now() - lastTlm) >= 1000) {
            lastTlm = now();
            const RadioStatus status = driverStatus_out(0);
            tlmWrite_RadioState(status.get_state());
            tlmWrite_LastRssi(status.get_rssi());
            tlmWrite_CrcDrops(status.get_crcDrops());
            tlmWrite_TxPackets(link.counters.tx);
            tlmWrite_RxPackets(link.counters.rx);
            tlmWrite_Retries(link.counters.retries);
            tlmWrite_AckTimeouts(link.counters.timeouts);
            tlmWrite_Drops(link.counters.drops);
            tlmWrite_Duplicates(link.counters.duplicates);
            tlmWrite_ReassemblyTimeouts(link.counters.reassemblyTimeouts);
        }
        rhWait();
    }
    driverPower_out(0, false);
    k_mutex_lock(&mutex, K_FOREVER);
    const bool cancelTest = testPending;
    const FwOpcodeType cancelOp = testOpcode;
    const U32 cancelSeq = testSequence;
    testPending = false;
    k_mutex_unlock(&mutex);
    if (cancelTest) finishTest(cancelOp, cancelSeq, false);
    k_mutex_lock(&mutex, K_FOREVER);
    if (pending) {
        auto buffer = txBuffer;
        auto context = txContext;
        pending = false;
        k_mutex_unlock(&mutex);
        dataReturnOut_out(0, buffer, context);
    } else
        k_mutex_unlock(&mutex);
}
}  // namespace Components
