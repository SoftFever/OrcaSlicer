cmake_minimum_required(VERSION 2.8.12)
project(miniz)

add_library(miniz INTERFACE)

if(NOT SLIC3R_STATIC OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
    
    if(${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
        target_compile_definitions(miniz_static PRIVATE _GNU_SOURCE)
    endif()

    target_link_libraries(miniz INTERFACE miniz_static)
    target_include_directories(miniz INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
    
    message(STATUS "Miniz NOT found in system, using bundled version...")

endif()
