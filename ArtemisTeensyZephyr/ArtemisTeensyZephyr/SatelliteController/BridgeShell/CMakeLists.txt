####
# F Prime CMakeLists.txt:
#
# SOURCES: list of source files (to be compiled)
# AUTOCODER_INPUTS: list of files to be passed to the autocoders
# DEPENDS: list of libraries that this module depends on
####

register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/BridgeShell.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/BridgeShell.cpp"
    DEPENDS
        ArtemisTeensyZephyr_SatelliteController_Zephyr
)
