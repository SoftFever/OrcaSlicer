project(clipper)
cmake_minimum_required(VERSION 2.6)

add_library(clipper STATIC
    clipper.cpp
    clipper.hpp
    clipper_z.cpp
    clipper_z.hpp
)

if(SLIC3R_PROFILE)
    target_link_libraries(clipper Shiny)
endif()
