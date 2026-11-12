set(RH_DIR "${FPRIME_PROJECT_ROOT}/lib/RadioHead")
add_library(artemis_radiohead STATIC
    ${RH_DIR}/RH_RF22.cpp ${RH_DIR}/RHSPIDriver.cpp
    ${RH_DIR}/RHGenericDriver.cpp ${RH_DIR}/RHGenericSPI.cpp RhZephyr.cpp)
target_include_directories(artemis_radiohead PUBLIC ${RH_DIR} ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(artemis_radiohead PUBLIC RH_PLATFORM=22)
target_link_libraries(artemis_radiohead PUBLIC zephyr_interface)
add_dependencies(artemis_radiohead offsets_h)
register_fprime_module(
    AUTOCODER_INPUTS "${CMAKE_CURRENT_LIST_DIR}/UHFRadioDriver_RFM23BP.fpp"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/UHFRadioDriver_RFM23BP.cpp"
    DEPENDS artemis_radiohead)
