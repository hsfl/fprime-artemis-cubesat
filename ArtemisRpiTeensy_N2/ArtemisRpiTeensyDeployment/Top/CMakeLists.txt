####
# F Prime CMakeLists.txt:
#
# SOURCES: list of source files (to be compiled)
# AUTOCODER_INPUTS: list of files to be passed to the autocoders
# DEPENDS: list of libraries that this module depends on
#
# More information in the F´ CMake API documentation:
# https://fprime.jpl.nasa.gov/latest/docs/reference/api/cmake/API/
#
####

set(NEUTRON2_TOPOLOGY_PROFILE "hil" CACHE STRING "Neutron 2 topology profile: hil or local-demo")
set_property(CACHE NEUTRON2_TOPOLOGY_PROFILE PROPERTY STRINGS hil local-demo)

if(NEUTRON2_TOPOLOGY_PROFILE STREQUAL "local-demo")
    set(NEUTRON2_TOPOLOGY_FPP "${CMAKE_CURRENT_LIST_DIR}/topology.local-demo.fpp")
elseif(NEUTRON2_TOPOLOGY_PROFILE STREQUAL "hil")
    set(NEUTRON2_TOPOLOGY_FPP "${CMAKE_CURRENT_LIST_DIR}/topology.fpp")
else()
    message(FATAL_ERROR "Unknown NEUTRON2_TOPOLOGY_PROFILE='${NEUTRON2_TOPOLOGY_PROFILE}'. Use 'hil' or 'local-demo'.")
endif()

message(STATUS "[Neutron2] Topology profile: ${NEUTRON2_TOPOLOGY_PROFILE} (${NEUTRON2_TOPOLOGY_FPP})")

register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/instances.fpp"
        "${NEUTRON2_TOPOLOGY_FPP}"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/ArtemisRpiTeensyDeploymentTopology.cpp"
    DEPENDS
        Fw_Logger
        RfMvpConfig
)
