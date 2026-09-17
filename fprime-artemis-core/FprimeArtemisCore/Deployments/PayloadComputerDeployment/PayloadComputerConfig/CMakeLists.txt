####
# Deployment-local configuration overrides for PayloadComputerDeployment.
#
# Registered under a unique name and EXCLUDE_FROM_ALL so it applies only to
# this deployment, leaving the project-wide packetizer limits untouched for the
# Zephyr deployments.
####
register_fprime_config(
    "PayloadComputerCdhCoreTlmConfig"
    CONFIGURATION_OVERRIDES
        "${CMAKE_CURRENT_LIST_DIR}/TlmPacketizerCfg.hpp"
    EXCLUDE_FROM_ALL
    INTERFACE
)
