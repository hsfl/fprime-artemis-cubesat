# Include project-wide components here

add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FatalHandler")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/NullPrmDb")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/MissionApp/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ComBufferAdapter/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/RpiPowerManager/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FlightControllerLinkManager/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/PayloadComputerLinkManager/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ImuDriver_LSM6DSOX/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ImuManager/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/GpsManager/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/GpsDriver_AdafruitMiniGps/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ThermalManager/")
# The TMP36 driver reads through fprime-zephyr's ZephyrADCDriver, so it only
# builds for the Teensy. ThermalManager stays portable.
if (ARTEMIS_TARGET_ZEPHYR)
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ThermalDriver_TMP36/")
endif()
# Payload computer components. PayloadManager and DpWrittenRouter are portable,
# but only PayloadComputerDeployment uses them, and the Lepton driver links
# libuvc, so all three are kept out of the Zephyr build.
if (NOT ARTEMIS_TARGET_ZEPHYR)
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/PayloadManager/")
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/PayloadDriver_Lepton/")
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/DpWrittenRouter/")
endif()
