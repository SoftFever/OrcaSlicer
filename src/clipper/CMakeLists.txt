cmake_minimum_required(VERSION 2.8.12)
project(clipper)

add_library(clipper STATIC
# We are using ClipperLib compiled as part of the libslic3r project using Slic3r::Point as its base type.
#    clipper.cpp
#    clipper.hpp
    clipper_z.cpp
    clipper_z.hpp
)

if(SLIC3R_PROFILE)
    target_link_libraries(clipper Shiny)
endif()
