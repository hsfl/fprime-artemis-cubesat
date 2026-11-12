register_fprime_module(
    AUTOCODER_INPUTS "${CMAKE_CURRENT_LIST_DIR}/EpsManager.fpp"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/EpsManager.cpp" "${CMAKE_CURRENT_LIST_DIR}/PduClient.cpp")
target_include_directories(${FPRIME_CURRENT_MODULE} PUBLIC "${FPRIME_PROJECT_ROOT}/../external/artemis-pdu/src")

register_fprime_ut(
    AUTOCODER_INPUTS "${CMAKE_CURRENT_LIST_DIR}/EpsManager.fpp"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/test/ut/EpsManagerTester.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/test/ut/EpsManagerTestMain.cpp"
    DEPENDS STest
    UT_AUTO_HELPERS)
