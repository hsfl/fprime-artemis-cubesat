register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/PayloadDriverSelector.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/PayloadDriverSelector.cpp"
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/PayloadDriverSelector.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadDriverSelectorTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadDriverSelectorTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
