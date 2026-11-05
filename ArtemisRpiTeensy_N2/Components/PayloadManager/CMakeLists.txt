register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/PayloadManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/PayloadManager.cpp
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/PayloadManager.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadManagerTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadManagerTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
