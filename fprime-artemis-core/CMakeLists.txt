####
# This sets up the build system for the 'fprime-artemis-core' project, including
# components and deployments from project.cmake. In addition, it imports the core F Prime components.
####

cmake_minimum_required(VERSION 3.24.2)
# Zephyr applies only to the Teensy flight target. F Prime unit tests run on
# Linux, and so do deployments like PayloadComputerDeployment, which is
# cross-compiled for the Raspberry Pi -- find_package(Zephyr) cannot succeed
# there. The toolchain file is the only signal available this early, since
# project() below is what actually reads it.
set(ARTEMIS_TARGET_ZEPHYR FALSE)
if (NOT BUILD_TESTING)
    if (NOT DEFINED CMAKE_TOOLCHAIN_FILE OR "${CMAKE_TOOLCHAIN_FILE}" MATCHES "[Zz]ephyr")
        set(ARTEMIS_TARGET_ZEPHYR TRUE)
    endif()
endif()

if (ARTEMIS_TARGET_ZEPHYR)
    find_package(Zephyr HINTS "${CMAKE_CURRENT_LIST_DIR}/lib/zephyr-workspace")
else()
    message(STATUS "Building without Zephyr (toolchain: ${CMAKE_TOOLCHAIN_FILE})")
endif()
project(fprime-artemis-core C CXX)

# Put in an option to force the device back into programable state for automated testing
option(FPRIME_CI_FAILSAFE_ENABLED "Cycle count before a forced reset to programable state" OFF)
if (FPRIME_CI_FAILSAFE_ENABLED)
    set(FPRIME_CI_FAILSAFE_CYCLE_COUNT 6000 CACHE STRING "Cycles before a forced reset")
    add_compile_definitions(FPRIME_CI_FAILSAFE_CYCLE_COUNT=${FPRIME_CI_FAILSAFE_CYCLE_COUNT})
endif()

###
# F' Core Setup
# This includes all of the F prime core components, and imports the make-system.
###
include("${CMAKE_CURRENT_LIST_DIR}/lib/fprime/cmake/FPrime.cmake")
# NOTE: register custom targets between these two lines
fprime_setup_included_code()

# This includes project-wide objects
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FprimeArtemisCore")

set_target_properties(Svc_FatalHandler PROPERTIES EXCLUDE_FROM_ALL TRUE)
if (ARTEMIS_TARGET_ZEPHYR)
    set_target_properties(fprime-zephyr_Drv_ZephyrSpiDriver PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
