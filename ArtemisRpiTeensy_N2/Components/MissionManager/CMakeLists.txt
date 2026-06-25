register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/MissionManager.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/MissionManager.cpp"
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/MissionManager.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/MissionManagerTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/MissionManagerTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
