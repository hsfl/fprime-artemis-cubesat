# This CMake file is intended to register project-wide objects.
# This allows for reuse between deployments, or other projects.

add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/project/config")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Components")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Deployments/ReferenceDeployment/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Deployments/FlightControllerDeployment/")
# Linux/RPi target - not buildable with the Zephyr toolchain
# add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Deployments/PayloadComputerDeployment/")
