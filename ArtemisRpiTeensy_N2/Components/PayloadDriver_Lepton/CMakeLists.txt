register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/PayloadDriver_Lepton.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/PayloadDriver_Lepton.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/LeptonCamera.cpp"
    DEPENDS
        Components_Types
        Subtopologies_ArtemisDataProducts_ArtemisDataProductsConfig
)

set(LEPTON_UVC_ENABLED OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # CMake's default multiarch search does not always descend into an ARM
    # sysroot's arm-linux-gnueabihf directory. Keep native Linux discovery,
    # but explicitly include the Pi sysroot locations used by our ARMv6 build.
    set(LEPTON_INCLUDE_HINTS)
    set(LEPTON_LIBRARY_HINTS)
    if(CMAKE_SYSROOT)
        list(APPEND LEPTON_INCLUDE_HINTS
            "${CMAKE_SYSROOT}/usr/local/include"
            "${CMAKE_SYSROOT}/usr/include"
        )
        list(APPEND LEPTON_LIBRARY_HINTS
            "${CMAKE_SYSROOT}/usr/local/lib"
            "${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf"
            "${CMAKE_SYSROOT}/lib/arm-linux-gnueabihf"
        )
    endif()

    find_library(LEPTON_UVC_LIB NAMES uvc HINTS ${LEPTON_LIBRARY_HINTS})
    find_path(LEPTON_UVC_INCLUDE_DIR NAMES libuvc/libuvc.h HINTS ${LEPTON_INCLUDE_HINTS})
    find_library(LEPTON_USB1_LIB NAMES usb-1.0 HINTS ${LEPTON_LIBRARY_HINTS})

    if(LEPTON_UVC_LIB AND LEPTON_UVC_INCLUDE_DIR AND LEPTON_USB1_LIB)
        set(LEPTON_UVC_ENABLED ON)
        message(STATUS "PayloadDriver_Lepton: libuvc enabled (${LEPTON_UVC_LIB})")
        target_compile_definitions(${FPRIME_CURRENT_MODULE} PRIVATE LEPTON_USE_LIBUVC=1)
        target_include_directories(${FPRIME_CURRENT_MODULE} PRIVATE "${LEPTON_UVC_INCLUDE_DIR}")
        target_link_libraries(${FPRIME_CURRENT_MODULE} PRIVATE "${LEPTON_UVC_LIB}" "${LEPTON_USB1_LIB}")
    else()
        message(STATUS "PayloadDriver_Lepton: libuvc disabled "
                       "(uvc=${LEPTON_UVC_LIB}, include=${LEPTON_UVC_INCLUDE_DIR}, usb=${LEPTON_USB1_LIB})")
    endif()
else()
    message(STATUS "PayloadDriver_Lepton: libuvc disabled for non-Linux host ${CMAKE_SYSTEM_NAME}")
endif()

if(LEPTON_UVC_ENABLED)
    add_executable(PayloadDriver_Lepton_testLeptonCamera
        "${CMAKE_CURRENT_LIST_DIR}/testLeptonCamera/testLeptonCamera.cpp"
    )
    set_target_properties(PayloadDriver_Lepton_testLeptonCamera PROPERTIES OUTPUT_NAME testLeptonCamera)
    target_include_directories(PayloadDriver_Lepton_testLeptonCamera PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/testLeptonCamera"
        "${LEPTON_UVC_INCLUDE_DIR}"
    )
    target_link_libraries(PayloadDriver_Lepton_testLeptonCamera PRIVATE
        "${LEPTON_UVC_LIB}"
        "${LEPTON_USB1_LIB}"
        pthread
    )
endif()
