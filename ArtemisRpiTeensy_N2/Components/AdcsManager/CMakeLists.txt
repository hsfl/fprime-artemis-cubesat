register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/AdcsManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/AdcsManager.cpp
    DEPENDS
        Components_Types
)
