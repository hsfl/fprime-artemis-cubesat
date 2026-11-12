set(ARTEMIS_RADIO_DRIVER UHFRadioDriver_RFM23BP)
if (ARTEMIS_TWIN)
    set(ARTEMIS_RADIO_DRIVER UHFRadioDriver_Sim)
endif()
configure_file(RadioComms.fpp.in ${CMAKE_CURRENT_BINARY_DIR}/RadioComms.fpp @ONLY)
register_fprime_module(
    AUTOCODER_INPUTS "${CMAKE_CURRENT_BINARY_DIR}/RadioComms.fpp"
    INTERFACE)
