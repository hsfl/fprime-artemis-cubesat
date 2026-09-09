# Hardware- and F'-agnostic protocol/radio-state core, ported from FprimeZephyrSatellite.
# Deliberately allocation-free; see RuntimeBridge.hpp for the callback-based hardware
# boundary. Host-side tests for this module are plain assert() executables under
# test/ and are not wired into this CMake build; run them directly with a host
# compiler (see docs/agents_notes.md for the invocation used to verify the port).

register_fprime_module(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/Protocol.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/RuntimeBridge.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/RadioStateMachine.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/LocalServices.cpp"
)
target_include_directories(${FPRIME_CURRENT_MODULE} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../include")
