cmake_minimum_required(VERSION 3.13)

project(nanosvg)

add_library(nanosvg INTERFACE)

target_include_directories(nanosvg INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(nanosvg INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/nanosvg.h
    ${CMAKE_CURRENT_SOURCE_DIR}/nanosvgrast.h
)