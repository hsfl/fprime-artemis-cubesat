register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/ScienceManager.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/ScienceManager.cpp"
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/ScienceManager.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ScienceManagerTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ScienceManagerTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
