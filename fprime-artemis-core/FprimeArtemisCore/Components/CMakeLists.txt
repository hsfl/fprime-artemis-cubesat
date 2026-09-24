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
