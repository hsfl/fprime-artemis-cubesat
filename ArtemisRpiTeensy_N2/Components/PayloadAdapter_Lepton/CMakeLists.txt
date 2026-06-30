####
# F Prime CMakeLists.txt:
#
# SOURCES: list of source files (to be compiled)
# AUTOCODER_INPUTS: list of files to be passed to the autocoders
# DEPENDS: list of libraries that this module depends on
#
# More information in the F´ CMake API documentation:
# https://fprime.jpl.nasa.gov/latest/docs/reference/api/cmake/API/
#
####

# Module names are derived from the path from the nearest project/library/framework
# root when not specifically overridden by the developer, i.e. the module defined by
# `MyProj/Some/Path/CMakeLists.txt` will be named `MyProj_Some_Path`.

register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/PayloadAdapter_Lepton.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/PayloadAdapter_Lepton.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/LeptonCamera.cpp"
#   DEPENDS
#       MyPackage_MyOtherModule
)

# Link libuvc for real FLIR Lepton capture. libuvc (and its libusb-1.0
# dependency) is installed on the Raspberry Pi as a system library; the Pi Zero W
# cross-build pulls it in via the synced sysroot (tools/sync_pi_zero_w_sysroot.sh
# already copies /usr/local and /usr/lib/arm-linux-gnueabihf). The cross
# toolchain sets CMAKE_FIND_ROOT_PATH_MODE_{LIBRARY,INCLUDE}=ONLY, so these
# find_*() calls resolve strictly inside the sysroot -- no HINTS/vendoring.
#
# Selection is automatic with no manual flags:
#   * Linux target + libuvc in sysroot -> define LEPTON_USE_LIBUVC, real capture
#   * otherwise (e.g. macOS native dev) -> deterministic ramp stub, so the
#     data-product path stays buildable/testable without the ARM camera stack.
find_library(UVC_LIB NAMES uvc)
find_path(UVC_INCLUDE_DIR NAMES libuvc/libuvc.h)

# if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND UVC_LIB AND UVC_INCLUDE_DIR)
if(UVC_LIB AND UVC_INCLUDE_DIR)
    message(STATUS "PayloadAdapter_Lepton: libuvc enabled (${UVC_LIB}) -- real Lepton capture")
    target_compile_definitions(${FPRIME_CURRENT_MODULE} PRIVATE LEPTON_USE_LIBUVC=1)
    target_include_directories(${FPRIME_CURRENT_MODULE} PRIVATE "${UVC_INCLUDE_DIR}")
    target_link_libraries(${FPRIME_CURRENT_MODULE} PRIVATE "${UVC_LIB}")
    # libuvc depends on libusb-1.0 at link/runtime (also in the synced sysroot).
    find_library(USB1_LIB NAMES usb-1.0)
    if(USB1_LIB)
        target_link_libraries(${FPRIME_CURRENT_MODULE} PRIVATE "${USB1_LIB}")
    else()
        message(WARNING "PayloadAdapter_Lepton: libusb-1.0 not found in sysroot; "
                        "libuvc link will fail (install libusb-1.0-0-dev on the Pi and re-sync the sysroot)")
    endif()
else()
    message(STATUS "PayloadAdapter_Lepton: using ramp stub capture "
                   "(system=${CMAKE_SYSTEM_NAME}, libuvc=${UVC_LIB})")
endif()

### Unit Tests ###
# register_fprime_ut(
#     AUTOCODER_INPUTS
#         "${CMAKE_CURRENT_LIST_DIR}/PayloadAdapter_Lepton.fpp"
#     SOURCES
#         "${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadAdapter_LeptonTestMain.cpp"
#         "${CMAKE_CURRENT_LIST_DIR}/test/ut/PayloadAdapter_LeptonTester.cpp"
#     DEPENDS
#         STest # For rules-based testing
#     UT_AUTO_HELPERS
# )
