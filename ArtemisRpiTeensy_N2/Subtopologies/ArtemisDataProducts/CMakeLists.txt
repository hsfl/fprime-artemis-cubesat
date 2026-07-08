add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ArtemisDataProductsConfig/")

register_fprime_module(
    EXCLUDE_FROM_ALL
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/ArtemisDataProducts.fpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/SubtopologyTopologyDefs.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/PingEntries.hpp"
    DEPENDS
        Subtopologies_ArtemisDataProducts_ArtemisDataProductsConfig
    INTERFACE
)
