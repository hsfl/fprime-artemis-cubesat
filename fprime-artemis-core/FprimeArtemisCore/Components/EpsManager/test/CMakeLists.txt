cmake_minimum_required(VERSION 3.18)
project(ArtemisPduClientTests LANGUAGES C CXX)
enable_testing()
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(PDU_ROOT "${REPO_ROOT}/external/artemis-pdu")
add_executable(pdu_client_tests PduClientTests.cpp ../PduClient.cpp)
target_compile_features(pdu_client_tests PRIVATE cxx_std_14)
target_include_directories(pdu_client_tests PRIVATE "${PDU_ROOT}/src")
target_compile_options(pdu_client_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME pdu_client_contract COMMAND pdu_client_tests)

# Isolate generated build metadata from the source checkout, as the twin's
# production host build does. The firmware and mock definitions are unchanged.
set(FIRMWARE "${CMAKE_CURRENT_BINARY_DIR}/firmware")
file(MAKE_DIRECTORY "${FIRMWARE}")
foreach(name app.c app.h pdu_packet.c pdu_packet.h pdu_protocol_v2.h version.h)
    configure_file("${PDU_ROOT}/src/${name}" "${FIRMWARE}/${name}" COPYONLY)
endforeach()
execute_process(COMMAND git -C "${PDU_ROOT}" rev-parse --short=12 HEAD OUTPUT_VARIABLE PDU_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
file(WRITE "${FIRMWARE}/pdu_build_info.h" "#define PDU_BUILD_GIT_HASH_KNOWN 1\n#define PDU_BUILD_GIT_DIRTY 0\n#define PDU_BUILD_GIT_HASH \"${PDU_HASH}\"\n")
add_executable(pdu_host_contract "${REPO_ROOT}/tools/twin/pdu_host.c" "${FIRMWARE}/app.c" "${FIRMWARE}/pdu_packet.c")
target_compile_features(pdu_host_contract PRIVATE c_std_11)
target_compile_definitions(pdu_host_contract PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(pdu_host_contract PRIVATE -Wall -Wextra -Werror)
target_include_directories(pdu_host_contract PRIVATE "${PDU_ROOT}/tests/host/include" "${FIRMWARE}")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME pdu_actual_firmware_interop COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_firmware.py"
    --probe "$<TARGET_FILE:pdu_client_tests>" --host "$<TARGET_FILE:pdu_host_contract>"
    --header "${PDU_ROOT}/src/pdu_protocol_v2.h")
