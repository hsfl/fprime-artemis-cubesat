####
# This sets up the build system for the 'ArtemisRpiTeensy_N2' project, including
# components and deployments from project.cmake. In addition, it imports the core F Prime components.
####

cmake_minimum_required(VERSION 3.13)
project(ArtemisRpiTeensy_N2 C CXX)

# Pi Zero W (ARMv6) requires explicit CPU/FPU flags for ARM Linux builds.
# Without these flags, generated binaries may emit instructions not valid on ARM1176.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    add_compile_options(-marm -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard)
    add_link_options(-marm -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard)
endif()

###
# F' Core Setup
# This includes all of the F prime core components, and imports the make-system.
###
include("${CMAKE_CURRENT_LIST_DIR}/lib/fprime/cmake/FPrime.cmake")
# NOTE: register custom targets between these two lines
fprime_setup_included_code()


# This includes project-wide objects
include("${CMAKE_CURRENT_LIST_DIR}/project.cmake")
