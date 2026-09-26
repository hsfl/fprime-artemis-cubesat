####
# Sizing, priorities, and paths for the ArtemisDataProducts subtopology.
# Payload drivers include the generated constants to predict DP file paths.
####
register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/ArtemisDataProductsConfig.fpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/ArtemisDataProductsSubtopologyConfig.hpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/ArtemisDataProductsSubtopologyConfig.cpp"
    DEPENDS
        Fw_Types
)
