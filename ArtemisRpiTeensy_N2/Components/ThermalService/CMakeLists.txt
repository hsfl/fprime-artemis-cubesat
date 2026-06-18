register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/ThermalService.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/ThermalService.cpp
    DEPENDS
        Components_Types
)
