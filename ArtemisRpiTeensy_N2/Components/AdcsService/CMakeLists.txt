register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/AdcsService.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/AdcsService.cpp
    DEPENDS
        Components_Types
)
