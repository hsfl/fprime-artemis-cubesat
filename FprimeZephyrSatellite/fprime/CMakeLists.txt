cmake_minimum_required(VERSION 3.24.2)

# Reuse the board configuration owned by the Zephyr application. Keeping the
# overlay and Kconfig in one place prevents the F' deployment and the probe
# application from silently targeting different UARTs or memory regions.
set(CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/../zephyr/prj.conf" CACHE FILEPATH "Zephyr application configuration")
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/../zephyr/boards/teensy41.overlay" CACHE FILEPATH "Zephyr device-tree overlay")
list(APPEND DTS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../zephyr")

if (NOT BUILD_TESTING)
    if (NOT DEFINED ENV{FPRIME_ZEPHYR_WORKSPACE})
        message(FATAL_ERROR "Set FPRIME_ZEPHYR_WORKSPACE to the directory containing Zephyr")
    endif()
    find_package(Zephyr HINTS "$ENV{FPRIME_ZEPHYR_WORKSPACE}")
endif()

project(FprimeZephyrSatellite C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../ArtemisRpiTeensy_N2/lib/fprime/cmake/FPrime.cmake")
fprime_setup_included_code()
include("${CMAKE_CURRENT_LIST_DIR}/project.cmake")
