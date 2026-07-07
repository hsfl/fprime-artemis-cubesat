register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/ScienceApp.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/ScienceApp.cpp"
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/ScienceApp.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ScienceAppTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ScienceAppTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
