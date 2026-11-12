set(ARTEMIS_GROUND_LINK "USB" CACHE STRING "Flight controller ground transport: USB or RFM23")
set_property(CACHE ARTEMIS_GROUND_LINK PROPERTY STRINGS USB RFM23)
if (ARTEMIS_GROUND_LINK STREQUAL "RFM23")
    set(GROUND_LINK_UART_OMIT "")
    set(GROUND_LINK_SCHED "")
    set(GROUND_LINK_INSTANCE "")
    set(GROUND_LINK_CONNECTIONS "connections RadioGroundLink {
      ComCcsds.FramingSubtopology.dataOut -> RadioComms.manager.dataIn
      RadioComms.manager.dataReturnOut -> ComCcsds.FramingSubtopology.dataReturnIn
      RadioComms.manager.comStatusOut -> ComCcsds.FramingSubtopology.comStatusIn
      RadioComms.manager.dataOut -> ComCcsds.FramingSubtopology.dataIn
      ComCcsds.FramingSubtopology.dataReturnOut -> RadioComms.manager.dataReturnIn
    }")
    add_compile_definitions(ARTEMIS_RADIO_GROUND_LINK=1)
elseif (ARTEMIS_GROUND_LINK STREQUAL "USB")
    set(GROUND_LINK_UART_OMIT "")
    foreach(channel RxBytes RxOverrunCount RxBackpressureCount RxAllocFailCount TxBytes TxDropCount)
        string(APPEND GROUND_LINK_UART_OMIT "  FprimeArtemisCore.comDriver.${channel}\n")
    endforeach()
    set(GROUND_LINK_SCHED "rateGroup_100Hz.RateGroupMemberOut[0] -> comDriver.schedIn")
    set(GROUND_LINK_INSTANCE "instance ComCcsds.comStub\n    instance comDriver")
    set(GROUND_LINK_CONNECTIONS "connections UsbGroundLink {
      ComCcsds.FramingSubtopology.dataOut -> ComCcsds.comStub.dataIn
      ComCcsds.comStub.dataReturnOut -> ComCcsds.FramingSubtopology.dataReturnIn
      ComCcsds.comStub.comStatusOut -> ComCcsds.FramingSubtopology.comStatusIn
      ComCcsds.comStub.dataOut -> ComCcsds.FramingSubtopology.dataIn
      ComCcsds.FramingSubtopology.dataReturnOut -> ComCcsds.comStub.dataReturnIn
      comDriver.allocate -> ComCcsds.commsBufferManager.bufferGetCallee
      comDriver.deallocate -> ComCcsds.commsBufferManager.bufferSendIn
      comDriver.$recv -> ComCcsds.comStub.drvReceiveIn
      ComCcsds.comStub.drvReceiveReturnOut -> comDriver.recvReturnIn
      ComCcsds.comStub.drvSendOut -> comDriver.$send
      comDriver.ready -> ComCcsds.comStub.drvConnected
    }")
else()
    message(FATAL_ERROR "ARTEMIS_GROUND_LINK must be USB or RFM23")
endif()
configure_file(FlightControllerDeploymentPackets.fppi.in ${CMAKE_CURRENT_BINARY_DIR}/FlightControllerDeploymentPackets.fppi @ONLY)
configure_file(topology.fpp.in ${CMAKE_CURRENT_BINARY_DIR}/topology.fpp @ONLY)
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

register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/instances.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/system.fpp"
        "${CMAKE_CURRENT_BINARY_DIR}/topology.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/FlightControllerDeploymentTopology.cpp"
    DEPENDS
        Fw_Logger
        # Pulls in the raised TlmPacketizer limits. Note that
        # CONFIGURATION_OVERRIDES applies tree-wide, not just to this target.
        FlightControllerCdhCoreTlmConfig
)
