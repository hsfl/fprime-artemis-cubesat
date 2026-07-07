register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/MissionApp.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/MissionApp.cpp"
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/MissionApp.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/MissionAppTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/MissionAppTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
