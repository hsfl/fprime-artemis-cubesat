# This CMake file registers the satellite RF/link controller: a hardware-agnostic
# protocol/radio-state core (Core/), Zephyr driver glue for the RFM23BP/UART/watchdog
# (Zephyr/, Zephyr-only), and the F' component that schedules it (BridgeShell/).

add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Core/")
if (FPRIME_PLATFORM STREQUAL "Zephyr")
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Zephyr/")
endif()
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/BridgeShell/")
