register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/PayloadDriver_NeutronSim.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/PayloadDriver_NeutronSim.cpp
    DEPENDS
        Components_Types
)
