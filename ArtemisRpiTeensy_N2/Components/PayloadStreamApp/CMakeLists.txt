register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/PayloadStreamApp.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/PayloadStreamApp.cpp
    HEADERS
        ${CMAKE_CURRENT_LIST_DIR}/PayloadStreamApp.hpp
    DEPENDS
        Components_LinkCfg
        Components_PayloadLinkTypes
)

set(UT_SOURCE_FILES
    ${CMAKE_CURRENT_LIST_DIR}/PayloadStreamApp.fpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadStreamAppTestMain.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadStreamAppTester.cpp
)

set(UT_MOD_DEPS
    Components_LinkCfg
    Components_PayloadLinkTypes
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
