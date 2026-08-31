register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/BridgeShell.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/BridgeShell.cpp"
)

# The F' adapter is deliberately thin: the allocation-free UART/RF parser and
# radio state machine remain the portable source of truth used by the Zephyr
# probe and host tests. This target only owns their F' lifecycle/port boundary.
target_include_directories(SatelliteController PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${CMAKE_CURRENT_LIST_DIR}/../../zephyr/include"
)
target_sources(SatelliteController PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../src/Protocol.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/RadioStateMachine.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/RuntimeBridge.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/LocalServices.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../zephyr/src/ControllerApp.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../zephyr/src/ControllerStorage.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../zephyr/src/ZephyrAdapters.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../zephyr/src/ZephyrUart.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../zephyr/src/ZephyrWatchdog.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../zephyr/src/Rfm23Zephyr.cpp"
)
