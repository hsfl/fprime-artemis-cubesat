register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/GpsManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/GpsManager.cpp
    DEPENDS
        Components_Types
)
