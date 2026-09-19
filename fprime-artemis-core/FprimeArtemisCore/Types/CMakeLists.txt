####
# Types shared by FlightControllerDeployment and PayloadComputerDeployment.
#
# Anything that crosses the FC<->PC link lives here, so both binaries are
# built from the same definition.
####
register_fprime_module(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/FcPcLink.fpp"
)
