# Zephyr driver glue for the satellite RF/link controller: RFM23BP SPI/GPIO driver,
# interrupt-driven UART rings, and the RT1062 watchdog adapter. Only built when
# targeting the Zephyr platform/toolchain; the portable core in ../Core has no
# dependency on this module.

register_fprime_module(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/ControllerApp.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/ControllerStorage.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/Rfm23Zephyr.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/ZephyrAdapters.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/ZephyrUart.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/ZephyrWatchdog.cpp"
    DEPENDS
        ArtemisTeensyZephyr_SatelliteController_Core
)
target_include_directories(${FPRIME_CURRENT_MODULE} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../include")
