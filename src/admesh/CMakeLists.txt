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

target_link_libraries(admesh PRIVATE boost_headeronly)
