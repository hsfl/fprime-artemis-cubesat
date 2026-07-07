register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/EpsManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/EpsManager.cpp
    DEPENDS
        Components_Types
)
