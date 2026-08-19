include(CheckIncludeFileCXX)

register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/PayloadDriver_Boson.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/PayloadDriver_Boson.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/BosonCamera.cpp"
    DEPENDS
        Components_Types
        Subtopologies_ArtemisDataProducts_ArtemisDataProductsConfig
)

set(BOSON_V4L2_ENABLED OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    check_include_file_cxx("linux/videodev2.h" BOSON_HAVE_VIDEODEV2_H)
    if(BOSON_HAVE_VIDEODEV2_H)
        set(BOSON_V4L2_ENABLED ON)
        target_compile_definitions(${FPRIME_CURRENT_MODULE} PRIVATE BOSON_USE_V4L2=1)
        message(STATUS "PayloadDriver_Boson: direct V4L2 enabled")
    else()
        message(STATUS "PayloadDriver_Boson: direct V4L2 disabled (linux/videodev2.h missing)")
    endif()
else()
    message(STATUS "PayloadDriver_Boson: direct V4L2 disabled for non-Linux host ${CMAKE_SYSTEM_NAME}")
endif()

add_executable(PayloadDriver_Boson_testBosonCamera
    "${CMAKE_CURRENT_LIST_DIR}/testBosonCamera/testBosonCamera.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/BosonCamera.cpp"
)
set_target_properties(PayloadDriver_Boson_testBosonCamera PROPERTIES OUTPUT_NAME testBosonCamera)
target_include_directories(PayloadDriver_Boson_testBosonCamera PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(PayloadDriver_Boson_testBosonCamera PRIVATE
    Fw
    Os
)
if(BOSON_V4L2_ENABLED)
    target_compile_definitions(PayloadDriver_Boson_testBosonCamera PRIVATE BOSON_USE_V4L2=1)
endif()
