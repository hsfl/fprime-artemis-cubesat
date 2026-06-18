register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/PayloadService.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/PayloadService.cpp
    DEPENDS
        Components_Types
)
