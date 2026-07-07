register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/TeensyTransportManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/TeensyTransportManager.cpp
    DEPENDS
        Components_Types
)
