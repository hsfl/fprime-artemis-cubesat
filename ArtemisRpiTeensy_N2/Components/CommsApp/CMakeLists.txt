register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/CommsApp.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/CommsApp.cpp
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/CommsApp.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/CommsAppTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/CommsAppTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
