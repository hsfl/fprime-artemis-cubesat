option(NEUTRON2_CONFIGURED_CAPTURE "Expose SCIENCE_CAPTURE without a duration argument" OFF)

set(SCIENCE_APP_FPP "${CMAKE_CURRENT_LIST_DIR}/ScienceApp.fpp")
if (NEUTRON2_CONFIGURED_CAPTURE)
    set(SCIENCE_APP_FPP "${CMAKE_CURRENT_LIST_DIR}/ScienceAppConfigured.fpp")
endif()

register_fprime_library(
    AUTOCODER_INPUTS
        "${SCIENCE_APP_FPP}"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/ScienceApp.cpp"
    DEPENDS
        Components_Types
)

set(UT_SOURCE_FILES
    "${SCIENCE_APP_FPP}"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ScienceAppTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/ScienceAppTester.cpp"
)

set(UT_MOD_DEPS
    Components_Types
    STest
)

register_fprime_ut()

if (NEUTRON2_CONFIGURED_CAPTURE)
    target_compile_definitions("${FPRIME_CURRENT_MODULE}" PUBLIC NEUTRON2_CONFIGURED_CAPTURE)
    set(SCIENCE_APP_UT_TARGET "${FPRIME_CURRENT_MODULE}_ut_exe")
    if (TARGET "${SCIENCE_APP_UT_TARGET}")
        target_compile_definitions("${SCIENCE_APP_UT_TARGET}" PRIVATE NEUTRON2_CONFIGURED_CAPTURE)
    endif()
endif()
