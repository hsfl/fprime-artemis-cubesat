register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/PayloadManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/PayloadManager.cpp
    DEPENDS
        Components_Types
)
