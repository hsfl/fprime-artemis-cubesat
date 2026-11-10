include("${CMAKE_CURRENT_LIST_DIR}/shared.cmake")

# Public simulator payload backend
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/PayloadManager/")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/PayloadDriver_NeutronSim/")
