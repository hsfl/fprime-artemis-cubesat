register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/SoHManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/SoHManager.cpp
    DEPENDS
        Components_Types
)
