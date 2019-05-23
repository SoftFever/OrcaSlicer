project(miniz)
cmake_minimum_required(VERSION 2.6)

add_library(miniz INTERFACE)

if(NOT SLIC3R_STATIC)
    find_package(miniz 2.1 QUIET)
endif() 

if(miniz_FOUND)
    
    message(STATUS "Using system miniz...")
    target_link_libraries(miniz INTERFACE miniz::miniz)

else()

    add_library(miniz_static STATIC
        miniz.c
        miniz.h
    )
    
    target_link_libraries(miniz INTERFACE miniz_static)
    target_include_directories(miniz INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
    
    message(STATUS "Miniz NOT found in system, using bundled version...")

endif()
