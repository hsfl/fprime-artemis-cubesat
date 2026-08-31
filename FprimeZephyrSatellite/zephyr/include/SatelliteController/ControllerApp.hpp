#pragma once

#include <cstddef>
#include <cstdint>

#include "SatelliteController/LocalServices.hpp"
#include "SatelliteController/Rfm23Zephyr.hpp"
#include "SatelliteController/RuntimeBridge.hpp"
#include "SatelliteController/ZephyrAdapters.hpp"
#include "SatelliteController/ZephyrUart.hpp"
#include "SatelliteController/ZephyrWatchdog.hpp"

namespace SatelliteController {

class ControllerApp final {
  public:
    ControllerApp();
    bool initialize();
    void poll(std::uint32_t nowMs);

    bool initialized() const { return m_initialized; }
    bool radioReady() const { return m_radio.ready(); }
    RfStatusSnapshot radioStatus() const { return m_rfStatus.status(); }
    std::uint32_t uartFramesReceived() const { return m_uartFramesReceived; }
    std::uint32_t wrongNetworkDrops() const { return m_wrongNetworkDrops; }
    std::uint32_t wrongAddressDrops() const { return m_wrongAddressDrops; }
    std::uint32_t wrongVersionDrops() const { return m_wrongVersionDrops; }
    const RuntimeBridge& bridge() const { return m_bridge; }

  private:
    static bool uartWrite(void* context, const std::uint8_t* bytes, std::size_t length);
    static bool rfSend(void* context, const std::uint8_t* bytes, std::size_t length);
    static bool localHandler(void* context, const Frame& request, Frame& response);
    static bool rfMessageHandler(void* context, const Frame& message);
    static void rfCompletionHandler(void* context, RuntimeBridge::TxTag tag, bool sent);
    static std::uint32_t clockNow(void* context);
    bool routeLocal(const Frame& request, Frame& response, std::uint32_t nowMs);
    void drainPiUart(std::uint32_t nowMs);
    void serviceRadio(std::uint32_t nowMs);
    void serviceLocalResponses(std::uint32_t nowMs);
    void serviceGeneratedPayload();
    void syncRadioState();

    Rfm23Zephyr m_radio;
    ZephyrUart m_piUart;
    ZephyrUart m_pduUart;
    ZephyrPduTransport m_pduTransport;
    ZephyrRfStatusProvider m_rfStatus;
    PduProxy m_pduProxy;
    PayloadCacheService m_payloadCache;
    PreviewService m_preview;
    LocalServicesRouter m_localRouter;
    RuntimeBridge m_bridge;
    ZephyrWatchdog m_watchdog;
    bool m_initialized = false;
    bool m_radioWasReady = false;
    std::uint32_t m_callbackNowMs = 0;
    std::uint32_t m_wrongNetworkDrops = 0;
    std::uint32_t m_wrongAddressDrops = 0;
    std::uint32_t m_wrongVersionDrops = 0;
    std::uint32_t m_uartFramesReceived = 0;
};

ControllerApp& controllerApp();
std::uint8_t* controllerPiRxStorage();
std::uint8_t* controllerPduRxStorage();
std::size_t controllerPiRxStorageSize();
std::size_t controllerPduRxStorageSize();
bool controllerAppInitialize();
void controllerAppPoll(std::uint32_t nowMs);

}  // namespace SatelliteController
