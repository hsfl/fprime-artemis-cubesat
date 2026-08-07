set(NEUTRON2_SPACECRAFT_PROFILE "n2-spacecraft-a" CACHE STRING
    "Neutron 2 spacecraft identity profile")
set_property(CACHE NEUTRON2_SPACECRAFT_PROFILE PROPERTY STRINGS
    n2-spacecraft-a
    n2-spacecraft-b
)

if(NEUTRON2_SPACECRAFT_PROFILE STREQUAL "n2-spacecraft-a")
    set(NEUTRON2_COM_CFG "${CMAKE_CURRENT_LIST_DIR}/ComCfg.n2-spacecraft-a.fpp")
elseif(NEUTRON2_SPACECRAFT_PROFILE STREQUAL "n2-spacecraft-b")
    set(NEUTRON2_COM_CFG "${CMAKE_CURRENT_LIST_DIR}/ComCfg.n2-spacecraft-b.fpp")
else()
    message(FATAL_ERROR
        "NEUTRON2_SPACECRAFT_PROFILE must be n2-spacecraft-a or n2-spacecraft-b; "
        "got '${NEUTRON2_SPACECRAFT_PROFILE}'")
endif()

message(STATUS "Neutron 2 spacecraft profile: ${NEUTRON2_SPACECRAFT_PROFILE}")
configure_file(
    "${NEUTRON2_COM_CFG}"
    "${CMAKE_CURRENT_BINARY_DIR}/ComCfg.fpp"
    COPYONLY
)

register_fprime_config(
    RfMvpConfig
    CONFIGURATION_OVERRIDES
        "${CMAKE_CURRENT_BINARY_DIR}/ComCfg.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/FpConstants.fpp"
    INTERFACE
)
