####
# ThermalDriver_TMP36 component
#
# Depends on fprime-zephyr's ZephyrADCDriver for the Zephyr.ADCMvValue port
# type, so it is registered for Zephyr builds only (see Components/CMakeLists.txt).
####
register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/ThermalDriver_TMP36.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/ThermalDriver_TMP36.cpp"
    DEPENDS
        fprime-zephyr_Drv_ZephyrADCDriver
)
