####
# default/config/CMakeLists.txt:
#
# Sets a list of source files for cmake to process as part of autocoding.
####
# Zephyr-only subtopology overrides.
#
# zephyr/CdhCoreConfig.fpp and zephyr/ComCcsdsConfig.fpp size active-component
# stacks to 8 KB and map priorities into Zephyr's 0..14 preemptible range,
# because Zephyr hands out fixed-size stacks from a preallocated pool
# (CONFIG_DYNAMIC_THREAD_STACK_SIZE in prj.conf) and rejects anything larger.
#
# Those values are wrong for Linux: 8 KB stacks get clamped to 16 KB with a
# warning per thread, and the 0..14 priorities mean something different. The
# Linux deployments therefore use the framework defaults, which are sized for a
# general-purpose OS.
#
# CONFIGURATION_OVERRIDES matches by file basename, so a file under zephyr/
# still overrides the framework's CdhCoreConfig.fpp when registered.
set(ARTEMIS_PLATFORM_CONFIG "")
if (FPRIME_PLATFORM STREQUAL "Zephyr")
    set(ARTEMIS_PLATFORM_CONFIG
        "${CMAKE_CURRENT_LIST_DIR}/zephyr/CdhCoreConfig.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/zephyr/ComCcsdsConfig.fpp"
    )
endif()

register_fprime_config(
    CONFIGURATION_OVERRIDES
        ${ARTEMIS_PLATFORM_CONFIG}
        "${CMAKE_CURRENT_LIST_DIR}/CdhCoreTlmConfig.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/CdhCoreFatalHandlerConfig.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/CommandDispatcherImplCfg.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/FpConfig.h"
        "${CMAKE_CURRENT_LIST_DIR}/PlatformCfg.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/TlmPacketizerCfg.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/TlmPacketizerCfg.hpp"
    INTERFACE
)
