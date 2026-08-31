#include "SatelliteController/ControllerApp.hpp"

namespace SatelliteController {
namespace {
__attribute__((section("OCRAM"), aligned(32))) ControllerApp g_controller;
// Interrupt-driven UART rings are CPU-only working data, so they deliberately
// use the fast 128 KiB DTCM bank rather than pretending all RT1062 RAM is one
// contiguous heap.
__attribute__((section("DTCM"), aligned(32))) std::uint8_t g_piRxStorage[4096];
__attribute__((section("DTCM"), aligned(32))) std::uint8_t g_pduRxStorage[256];
}

ControllerApp& controllerApp() { return g_controller; }
std::uint8_t* controllerPiRxStorage() { return g_piRxStorage; }
std::uint8_t* controllerPduRxStorage() { return g_pduRxStorage; }
std::size_t controllerPiRxStorageSize() { return sizeof(g_piRxStorage); }
std::size_t controllerPduRxStorageSize() { return sizeof(g_pduRxStorage); }

}  // namespace SatelliteController
