register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/UartChannelMux.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/UartChannelMux.cpp
    HEADERS
        ${CMAKE_CURRENT_LIST_DIR}/UartChannelMux.hpp
    DEPENDS
        Components_LinkCfg
)

set(UT_SOURCE_FILES
    ${CMAKE_CURRENT_LIST_DIR}/UartChannelMux.fpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/UartChannelMuxTestMain.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/UartChannelMuxTester.cpp
)

set(UT_MOD_DEPS
    Components_LinkCfg
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
