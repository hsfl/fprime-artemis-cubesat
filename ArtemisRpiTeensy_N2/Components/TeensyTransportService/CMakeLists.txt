register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/TeensyTransportService.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/TeensyTransportService.cpp
    DEPENDS
        Components_Types
)
