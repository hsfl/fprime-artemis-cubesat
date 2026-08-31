cmake_minimum_required(VERSION 3.20)
project(FprimeZephyrSatelliteCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(satellite_controller_core
    src/Protocol.cpp
    src/RuntimeBridge.cpp
    src/RadioStateMachine.cpp
    src/LocalServices.cpp
)
target_include_directories(satellite_controller_core PUBLIC include)
target_compile_options(satellite_controller_core PRIVATE -Wall -Wextra -Werror -pedantic)

enable_testing()
add_executable(satellite_controller_core_tests test/CoreTests.cpp)
target_link_libraries(satellite_controller_core_tests PRIVATE satellite_controller_core)
target_compile_options(satellite_controller_core_tests PRIVATE -Wall -Wextra -Werror -pedantic)
add_test(NAME satellite_controller_core_tests COMMAND satellite_controller_core_tests)

add_executable(satellite_controller_local_services_tests test/LocalServicesTests.cpp)
target_link_libraries(satellite_controller_local_services_tests PRIVATE satellite_controller_core)
target_compile_options(satellite_controller_local_services_tests PRIVATE -Wall -Wextra -Werror -pedantic)
add_test(NAME satellite_controller_local_services_tests COMMAND satellite_controller_local_services_tests)

add_executable(satellite_controller_runtime_bridge_tests test/RuntimeBridgeTests.cpp)
target_link_libraries(satellite_controller_runtime_bridge_tests PRIVATE satellite_controller_core)
target_compile_options(satellite_controller_runtime_bridge_tests PRIVATE -Wall -Wextra -Werror -pedantic)
add_test(NAME satellite_controller_runtime_bridge_tests COMMAND satellite_controller_runtime_bridge_tests)
