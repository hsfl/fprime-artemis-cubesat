register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/SoHApp.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/SoHApp.cpp
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/SoHApp.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/SoHAppTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/SoHAppTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
