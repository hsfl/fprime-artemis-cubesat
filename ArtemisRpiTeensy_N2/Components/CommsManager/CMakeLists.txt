register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/CommsManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/CommsManager.cpp
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/CommsManager.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/CommsManagerTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/CommsManagerTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
