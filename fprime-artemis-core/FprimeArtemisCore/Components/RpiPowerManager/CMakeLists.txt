####
# RpiPowerManager component
####
register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/RpiPowerManager.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/RpiPowerManager.cpp"
)
