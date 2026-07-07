register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/ThermalManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/ThermalManager.cpp
    DEPENDS
        Components_Types
)
