cmake_minimum_required(VERSION 3.18)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ArtemisScience LANGUAGES CXX)
    enable_testing()
    set(ARTEMIS_SCIENCE_STANDALONE ON)
endif()

add_library(artemis_science STATIC ProductCache.cpp)
target_compile_features(artemis_science PUBLIC cxx_std_14)
target_include_directories(artemis_science PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")

if(ARTEMIS_SCIENCE_STANDALONE)
    target_compile_options(artemis_science PRIVATE -Wall -Wextra -Werror -Wconversion)
    add_executable(science_cache_tests test/ProductCacheTests.cpp)
    target_link_libraries(science_cache_tests PRIVATE artemis_science)
    target_compile_options(science_cache_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME science_cache COMMAND science_cache_tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME science_legacy_receiver
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/verify_receiver.py"
            --executable "$<TARGET_FILE:science_cache_tests>"
            --receiver "${CMAKE_CURRENT_SOURCE_DIR}/../../../ArtemisRpiTeensy_N2/tools/payload_receiver.py")
endif()
