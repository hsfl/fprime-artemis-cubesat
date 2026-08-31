cmake_minimum_required(VERSION 3.24.2)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(satellite_teensy41_controller C CXX)

target_include_directories(app PRIVATE include ../include)
target_sources(app PRIVATE
    src/main.cpp
    src/ControllerApp.cpp
    src/ControllerStorage.cpp
    src/Rfm23Zephyr.cpp
    src/ZephyrAdapters.cpp
    src/ZephyrUart.cpp
    src/ZephyrWatchdog.cpp
    ../src/LocalServices.cpp
    ../src/Protocol.cpp
    ../src/RuntimeBridge.cpp
    ../src/RadioStateMachine.cpp
)
