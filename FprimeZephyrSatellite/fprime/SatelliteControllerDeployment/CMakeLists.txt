add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Top/")

if (FPRIME_PLATFORM STREQUAL "Zephyr")
    register_fprime_zephyr_deployment(
        ${FPRIME_CURRENT_MODULE}
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/Main.cpp"
        DEPENDS
            ${FPRIME_CURRENT_MODULE}_Top
    )
else()
    register_fprime_deployment(
        ${FPRIME_CURRENT_MODULE}
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/Main.cpp"
        DEPENDS
            ${FPRIME_CURRENT_MODULE}_Top
    )
endif()
