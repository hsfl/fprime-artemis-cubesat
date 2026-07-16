register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/CommsDriver_TeensyRfm23.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/CommsDriver_TeensyRfm23.cpp
    DEPENDS
        Components_Types
        Components_LinkCfg
)

set(UT_SOURCE_FILES
    ${CMAKE_CURRENT_LIST_DIR}/CommsDriver_TeensyRfm23.fpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/CommsDriver_TeensyRfm23TestMain.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/CommsDriver_TeensyRfm23Tester.cpp
)

set(UT_MOD_DEPS
    Components_Types
    Components_LinkCfg
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
