register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/PayloadDownlinkApp.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/PayloadDownlinkApp.cpp
    HEADERS
        ${CMAKE_CURRENT_LIST_DIR}/PayloadDownlinkApp.hpp
    DEPENDS
        Components_LinkCfg
        Components_Types
)

set(UT_SOURCE_FILES
    ${CMAKE_CURRENT_LIST_DIR}/PayloadDownlinkApp.fpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadDownlinkAppTestMain.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadDownlinkAppTester.cpp
)

set(UT_MOD_DEPS
    Components_LinkCfg
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
