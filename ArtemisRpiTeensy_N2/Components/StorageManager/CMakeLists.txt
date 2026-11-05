register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/StorageManager.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/StorageManager.cpp
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/StorageManager.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/StorageManagerTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/StorageManagerTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
