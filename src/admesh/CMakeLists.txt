project(admesh)
cmake_minimum_required(VERSION 2.6)

add_library(admesh STATIC
    connect.cpp
    normals.cpp
    shared.cpp
    stl.h
    stl_io.cpp
    stlinit.cpp
    util.cpp
)

target_include_directories(admesh SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
