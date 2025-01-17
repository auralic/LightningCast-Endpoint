cmake_minimum_required(VERSION 3.10)

project(LightningCastDeviceSample)

set(CMAKE_CXX_STANDARD 11)

# Include directories
include_directories(.)

# Source files
set(SOURCES
    LightningCastDeviceSample.cpp
    Util.cpp
    jsoncpp.cpp
    LightningCastClient.cpp
    LightningCastServer.cpp
    SnapcastComm.cpp
    tinysvcmdns.cpp
)

# Executable
add_executable(LightningCastDeviceSample ${SOURCES})

# Link libraries
target_link_libraries(LightningCastDeviceSample pthread)
