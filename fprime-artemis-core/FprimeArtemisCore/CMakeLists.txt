# This CMake file is intended to register project-wide objects.
# This allows for reuse between deployments, or other projects.

add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/project/config")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Components")
# Deployments are registered per target. The Zephyr ones need the Zephyr
# package; PayloadComputerDeployment targets Linux/RPi and is built with the
# ARM cross toolchain (see tools/cross/cross_compile.sh).
# Zephyr builds exactly one application image per build tree, and
# register_fprime_zephyr_deployment asserts on a second one. FlightControllerDeployment
# is the flight target, so it holds that slot; ReferenceDeployment is kept in the tree
# as the vendor sample but is not registered.
if (ARTEMIS_TARGET_ZEPHYR)
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Deployments/FlightControllerDeployment/")
else()
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Deployments/PayloadComputerDeployment/")
endif()
