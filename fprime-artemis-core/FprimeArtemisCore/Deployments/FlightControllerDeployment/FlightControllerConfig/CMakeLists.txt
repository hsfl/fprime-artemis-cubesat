####
# Deployment-local configuration overrides for FlightControllerDeployment.
#
# CONFIGURATION_OVERRIDES replaces a file for the whole build tree, not per
# deployment, so ReferenceDeployment gets these packetizer limits too. That is
# safe: its packet set is smaller than FlightControllerDeployment's.
####
register_fprime_config(
    "FlightControllerCdhCoreTlmConfig"
    CONFIGURATION_OVERRIDES
        "${CMAKE_CURRENT_LIST_DIR}/TlmPacketizerCfg.hpp"
    EXCLUDE_FROM_ALL
    INTERFACE
)
