register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/PayloadAdapter_NeutronSim.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/PayloadAdapter_NeutronSim.cpp
    DEPENDS
        Components_Types
)
