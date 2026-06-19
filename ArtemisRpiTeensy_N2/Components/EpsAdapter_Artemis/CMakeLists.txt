register_fprime_library(
    AUTOCODER_INPUTS
        ${CMAKE_CURRENT_LIST_DIR}/EpsAdapter_Artemis.fpp
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/EpsAdapter_Artemis.cpp
    HEADERS
        ${CMAKE_CURRENT_LIST_DIR}/EpsAdapter_Artemis.hpp
    DEPENDS
        Components_Types
        Components_LinkCfg
)

set(UT_SOURCE_FILES
    ${CMAKE_CURRENT_LIST_DIR}/EpsAdapter_Artemis.fpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/EpsAdapter_ArtemisTestMain.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test/ut/EpsAdapter_ArtemisTester.cpp
)

set(UT_MOD_DEPS
    Components_Types
    Components_LinkCfg
    STest
)

set(UT_AUTO_HELPERS ON)
register_fprime_ut()
