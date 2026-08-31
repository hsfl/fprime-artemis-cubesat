#include "SatelliteController/ControllerApp.hpp"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

namespace SatelliteController {
namespace {
const gpio_dt_spec RPI_ENABLE = GPIO_DT_SPEC_GET(DT_PATH(satellite_control), rpi_enable_gpios);
}

ControllerApp::ControllerApp()
    : m_radio(),
      m_piUart(DEVICE_DT_GET(DT_NODELABEL(lpuart4)), controllerPiRxStorage(), controllerPiRxStorageSize()),
      m_pduUart(DEVICE_DT_GET(DT_NODELABEL(lpuart6)), controllerPduRxStorage(), controllerPduRxStorageSize()),
      m_pduTransport(m_pduUart),
      m_rfStatus(m_radio),
      m_pduProxy(m_pduTransport),
      m_payloadCache(),
      m_preview(),
      m_localRouter(m_rfStatus, m_pduProxy, m_payloadCache, m_preview),
      m_bridge(RuntimeBridge::Callbacks{this, uartWrite, rfSend, localHandler, rfMessageHandler,
                                        rfCompletionHandler, clockNow}),
      m_watchdog() {}

bool ControllerApp::initialize() {
    if (m_initialized) return true;
    const auto safeOff = m_radio.configureSafeOff();
    const bool rpiReady = gpio_is_ready_dt(&RPI_ENABLE) &&
                          gpio_pin_configure_dt(&RPI_ENABLE, GPIO_OUTPUT_ACTIVE) == 0;
    const bool piUartReady = m_piUart.initialize();
    const bool pduUartReady = m_pduUart.initialize();
    const bool watchdogReady = m_watchdog.initialize(WatchdogModel::TIMEOUT_MS);
    m_rfStatus.setBootWatchdogReset(m_watchdog.watchdogReset());
    m_bridge.setRfEnabled(false);
    m_initialized = safeOff == Rfm23Zephyr::Result::OK && rpiReady && piUartReady &&
                    pduUartReady && watchdogReady;
    printk("satellite controller safe-off=%d rpi=%d pi-uart=%d pdu-uart=%d wdt=%d reset-wdt=%d\n",
           static_cast<int>(safeOff), rpiReady, piUartReady, pduUartReady,
           watchdogReady, m_watchdog.watchdogReset());
    return m_initialized;
}

bool ControllerApp::uartWrite(void* context, const std::uint8_t* bytes, std::size_t length) {
    auto* self = static_cast<ControllerApp*>(context);
    return self != nullptr && self->m_piUart.write(bytes, length);
}

bool ControllerApp::rfSend(void* context, const std::uint8_t* bytes, std::size_t length) {
    auto* self = static_cast<ControllerApp*>(context);
    const bool canonicalAck = length == Protocol::RF_SEGMENT_HEADER_LEN &&
                              bytes != nullptr && bytes[2] == Protocol::RF_ACK_INDEX &&
                              bytes[4] == 0;
    if (self == nullptr || !self->m_radio.ready()) {
        if (self != nullptr && !canonicalAck) self->m_rfStatus.noteTransmitResult(false);
        return false;
    }
    Rfm23Zephyr::Result result = self->m_radio.sendPacket(bytes, length);
    // Match the Arduino bridge: retry exactly once only after the low-level
    // timeout path has cleared both FIFOs and restored RX. Start failures are
    // terminal because no completed recovery is known to have occurred.
    if (result == Rfm23Zephyr::Result::TX_TIMEOUT) {
        (void)self->m_watchdog.feed();
        result = self->m_radio.sendPacket(bytes, length);
    }
    if (result != Rfm23Zephyr::Result::OK) self->m_radio.failSafeOffLocalTx();
    if (!canonicalAck) self->m_rfStatus.noteTransmitResult(result == Rfm23Zephyr::Result::OK);
    return result == Rfm23Zephyr::Result::OK;
}

bool ControllerApp::localHandler(void* context, const Frame& request, Frame& response) {
    auto* self = static_cast<ControllerApp*>(context);
    return self != nullptr && self->routeLocal(request, response, self->m_callbackNowMs);
}

bool ControllerApp::rfMessageHandler(void* context, const Frame& message) {
    auto* self = static_cast<ControllerApp*>(context);
    return self != nullptr && message.channel == Protocol::CHANNEL_PAYLOAD &&
           self->m_payloadCache.handlePayloadControl(message.payload.data(), message.length);
}

void ControllerApp::rfCompletionHandler(void* context, RuntimeBridge::TxTag tag, bool sent) {
    auto* self = static_cast<ControllerApp*>(context);
    if (self == nullptr) return;
    if (tag == RuntimeBridge::TxTag::PAYLOAD_CACHE) self->m_payloadCache.payloadPacketSent(sent);
    else if (tag == RuntimeBridge::TxTag::PREVIEW) self->m_preview.previewPacketAttempted(sent);
}

std::uint32_t ControllerApp::clockNow(void*) { return k_uptime_get_32(); }

bool ControllerApp::routeLocal(const Frame& request, Frame& response, std::uint32_t nowMs) {
    if (!m_localRouter.beginLocalFrame(request.payload.data(), request.length, nowMs)) return false;
    std::size_t length = 0;
    if (!m_localRouter.pollLocalResponse(nowMs, response.payload.data(), response.payload.size(), length)) return false;
    response.channel = Protocol::CHANNEL_LOCAL;
    response.length = static_cast<std::uint16_t>(length);
    return length != 0;
}

void ControllerApp::drainPiUart(std::uint32_t nowMs) {
    std::uint8_t byte = 0;
    m_callbackNowMs = nowMs;
    while (m_piUart.readByte(byte)) {
        if (m_bridge.ingestUartByte(byte, nowMs) == ParseResult::FRAME) ++m_uartFramesReceived;
        // A completed channel-2 request can change radio readiness. Apply it
        // before the next framed channel in the same UART burst is routed.
        syncRadioState();
    }
}

void ControllerApp::serviceRadio(std::uint32_t nowMs) {
    // serviceInterrupt owns the atomic consume; checking irqPending here would
    // clear the work signal before the driver can read the status registers.
    (void)m_radio.serviceInterrupt();
    std::uint8_t packet[Protocol::RF_PACKET_MAX_LEN]{};
    std::size_t length = 0;
    while (m_radio.available()) {
        if (m_radio.receivePacket(packet, sizeof(packet), length) != Rfm23Zephyr::Result::OK) break;
        const Rfm23Zephyr::Header& header = m_radio.lastReceivedHeader();
        switch (classifyRfHeader(header.to, header.from, header.id, header.flags)) {
            case RfHeaderStatus::ACCEPT:
                m_radio.markLastPacketAccepted();
                if (m_bridge.ingestRfPacket(packet, length, nowMs) != RuntimeBridge::RfResult::ACK) {
                    m_rfStatus.noteAcceptedPacket();
                }
                break;
            case RfHeaderStatus::WRONG_NETWORK: ++m_wrongNetworkDrops; break;
            case RfHeaderStatus::WRONG_ADDRESS: ++m_wrongAddressDrops; break;
            case RfHeaderStatus::WRONG_VERSION: ++m_wrongVersionDrops; break;
        }
    }
}

void ControllerApp::serviceLocalResponses(std::uint32_t nowMs) {
    std::uint8_t response[Protocol::FRAME_MAX_PAYLOAD]{};
    std::size_t length = 0;
    if (m_localRouter.pollLocalResponse(nowMs, response, sizeof(response), length)) {
        (void)m_bridge.queueLocalResponse(response, length);
    }
}

void ControllerApp::serviceGeneratedPayload() {
    if (!m_radio.ready() || m_bridge.pendingRfMessages() != 0) return;
    std::uint8_t payload[Protocol::FRAME_MAX_PAYLOAD]{};
    std::size_t length = 0;
    if (m_payloadCache.nextPayloadPacket(payload, sizeof(payload), length)) {
        const bool queued = m_bridge.queueRfMessage(Protocol::CHANNEL_PAYLOAD, payload, length,
                                                     RuntimeBridge::TxTag::PAYLOAD_CACHE);
        if (!queued) m_payloadCache.payloadPacketSent(false);
        return;
    }
    if (m_preview.nextPreviewPacket(payload, Protocol::RF_SEGMENT_MAX_DATA, length)) {
        const bool queued = m_bridge.queueRfMessage(Protocol::CHANNEL_PAYLOAD, payload, length,
                                                     RuntimeBridge::TxTag::PREVIEW);
        if (!queued) m_preview.previewPacketAttempted(false);
    }
}

void ControllerApp::syncRadioState() {
    const bool ready = m_radio.ready();
    if (m_radioWasReady && !ready) {
        // Arduino counts every queued uplink discarded by a radio-off
        // transition as a TX drop. Completion callbacks still unwind any
        // cache/preview producer state inside discardRadioWork().
        m_rfStatus.noteTransmitDrops(m_bridge.pendingRfMessages());
        m_bridge.discardRadioWork();
    }
    m_radioWasReady = ready;
    m_bridge.setRfEnabled(ready);
}

void ControllerApp::poll(std::uint32_t nowMs) {
    if (!m_initialized) return;
    (void)m_watchdog.feed();
    syncRadioState();
    drainPiUart(nowMs);
    serviceRadio(nowMs);
    syncRadioState();
    serviceLocalResponses(nowMs);
    serviceGeneratedPayload();
    m_bridge.poll(k_uptime_get_32(), !m_radio.receiveInProgress());
    syncRadioState();
}

bool controllerAppInitialize() { return controllerApp().initialize(); }
void controllerAppPoll(std::uint32_t nowMs) { controllerApp().poll(nowMs); }

}  // namespace SatelliteController
