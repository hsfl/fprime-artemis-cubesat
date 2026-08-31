# Register only the dedicated, minimal Teensy deployment here. Do not use the
# standard deployment generator's CdhCore/ComCcsds/DataProducts/FileHandling
# topology: those services remain on the Raspberry Pi mission deployment.
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ZephyrConfig/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/SatelliteController/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/SatelliteControllerDeployment/")
