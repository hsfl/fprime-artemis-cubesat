# Consumers can supply their own payload instances, topology, and ping entries
# while building the same deployment sources.
if (NOT DEFINED NEUTRON2_TOPOLOGY_INPUTS)
    set(NEUTRON2_TOPOLOGY_INPUTS "${CMAKE_CURRENT_LIST_DIR}/instances.fpp")
    if (NEUTRON2_CDR_TCP)
        list(APPEND NEUTRON2_TOPOLOGY_INPUTS
            "${CMAKE_CURRENT_LIST_DIR}/instances.cdr_tcp.fpp"
            "${CMAKE_CURRENT_LIST_DIR}/topology.cdr_tcp.fpp"
        )
    else()
        list(APPEND NEUTRON2_TOPOLOGY_INPUTS "${CMAKE_CURRENT_LIST_DIR}/topology.fpp")
    endif()
endif()

set(NEUTRON2_TOPOLOGY_DEPENDENCIES Fw_Logger ${NEUTRON2_DEPLOYMENT_CONFIG})
if (NEUTRON2_CDR_TCP)
    list(APPEND NEUTRON2_TOPOLOGY_DEPENDENCIES Drv_TcpServer)
endif()

register_fprime_module(
    AUTOCODER_INPUTS ${NEUTRON2_TOPOLOGY_INPUTS}
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/ArtemisRpiTeensyDeploymentTopology.cpp"
    DEPENDS ${NEUTRON2_TOPOLOGY_DEPENDENCIES}
)

if (NOT DEFINED NEUTRON2_PAYLOAD_PING_INCLUDE_DIR)
    set(NEUTRON2_PAYLOAD_PING_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()
target_include_directories(${FPRIME_CURRENT_MODULE} PUBLIC "${NEUTRON2_PAYLOAD_PING_INCLUDE_DIR}")
