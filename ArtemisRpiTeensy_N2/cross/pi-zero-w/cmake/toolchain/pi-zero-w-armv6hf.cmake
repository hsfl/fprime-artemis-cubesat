####
# Pi Zero W ARMv6 hard-float toolchain
#
# Reuses the F' ARM Linux helper while forcing flags that are safe for the
# Raspberry Pi Zero W Rev 1.1 CPU and ABI.
####

set(CMAKE_SYSTEM_PROCESSOR "arm")
set(ARM_TOOL_SUFFIX eabihf)
set(FPRIME_TOOLCHAIN_NAME "pi-zero-w-armv6hf" CACHE INTERNAL "F' toolchain name" FORCE)

# Avoid try-run checks while cross compiling.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../lib/fprime/cmake/toolchain/helpers/arm-linux-base.cmake")

set(PI_ZERO_W_CPU_FLAGS "-marm -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard")
set(PI_ZERO_W_RUNTIME_FLAGS "")

if(DEFINED CMAKE_SYSROOT)
  file(GLOB PI_ZERO_W_GCC_RUNTIME_CANDIDATES LIST_DIRECTORIES true "${CMAKE_SYSROOT}/usr/lib/gcc/arm-linux-gnueabihf/*")
  list(SORT PI_ZERO_W_GCC_RUNTIME_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
  list(GET PI_ZERO_W_GCC_RUNTIME_CANDIDATES 0 PI_ZERO_W_GCC_RUNTIME_DIR)

  if(NOT EXISTS "${PI_ZERO_W_GCC_RUNTIME_DIR}/crtbeginS.o")
    message(FATAL_ERROR "Pi Zero W sysroot is missing GCC runtime objects under ${CMAKE_SYSROOT}/usr/lib/gcc/arm-linux-gnueabihf")
  endif()

  message(STATUS "Pi Zero W GCC runtime dir: ${PI_ZERO_W_GCC_RUNTIME_DIR}")

  string(JOIN " " PI_ZERO_W_RUNTIME_FLAGS
      "-B${PI_ZERO_W_GCC_RUNTIME_DIR}"
      "-B${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf"
      "-B${CMAKE_SYSROOT}/lib/arm-linux-gnueabihf"
      "-Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf"
      "-Wl,-rpath-link,${CMAKE_SYSROOT}/lib/arm-linux-gnueabihf")
endif()

string(APPEND CMAKE_C_FLAGS_INIT " ${PI_ZERO_W_CPU_FLAGS} ${PI_ZERO_W_RUNTIME_FLAGS}")
string(APPEND CMAKE_CXX_FLAGS_INIT " ${PI_ZERO_W_CPU_FLAGS} ${PI_ZERO_W_RUNTIME_FLAGS}")
string(APPEND CMAKE_ASM_FLAGS_INIT " ${PI_ZERO_W_CPU_FLAGS}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${PI_ZERO_W_CPU_FLAGS} ${PI_ZERO_W_RUNTIME_FLAGS}")
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " ${PI_ZERO_W_CPU_FLAGS} ${PI_ZERO_W_RUNTIME_FLAGS}")
