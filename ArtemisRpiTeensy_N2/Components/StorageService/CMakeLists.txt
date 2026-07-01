register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/StorageService.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/StorageService.cpp
    DEPENDS
        Components_Types
)
