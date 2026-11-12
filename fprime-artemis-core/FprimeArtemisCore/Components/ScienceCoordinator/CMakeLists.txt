register_fprime_module(
    AUTOCODER_INPUTS "${CMAKE_CURRENT_LIST_DIR}/ScienceCoordinator.fpp"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/ScienceCoordinator.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/../../../Platform/CacheStorage.cpp"
    DEPENDS artemis_science)
register_fprime_ut(
    AUTOCODER_INPUTS "${CMAKE_CURRENT_LIST_DIR}/ScienceCoordinator.fpp"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/test/ut/ScienceCoordinatorTester.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/test/ut/ScienceCoordinatorTestMain.cpp"
    UT_AUTO_HELPERS
)
