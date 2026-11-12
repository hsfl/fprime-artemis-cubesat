register_fprime_module(
    AUTOCODER_INPUTS "${CMAKE_CURRENT_LIST_DIR}/PayloadLinkService.fpp"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/PayloadLinkService.cpp"
    DEPENDS FprimeArtemisCore_Types
)
register_fprime_ut(
    AUTOCODER_INPUTS "${CMAKE_CURRENT_LIST_DIR}/PayloadLinkService.fpp"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadLinkServiceTester.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadLinkServiceTestMain.cpp"
    UT_AUTO_HELPERS
)
