project(libnoise)
cmake_minimum_required(VERSION 3.0)

add_library(libnoise INTERFACE)

# Define the source directory
set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB NOISE_SOURCES_LIST
    "${SOURCE_DIR}/*.cpp"
    "${SOURCE_DIR}/model/*.cpp"
    "${SOURCE_DIR}/module/*.cpp")

file(GLOB NOISE_HEADERS_LIST
    "${SOURCE_DIR}/*.h"
    "${SOURCE_DIR}/model/*.h"
    "${SOURCE_DIR}/module/*.h")

# Conditionally include win32 files if compiling on Windows
if(WIN32)
    set(WIN32_DIR "${SOURCE_DIR}/win32")
    file(GLOB WIN32_SOURCES "${WIN32_DIR}/*.cpp")
    file(GLOB WIN32_HEADERS "${WIN32_DIR}/*.h")
    list(APPEND NOISE_SOURCES_LIST ${WIN32_SOURCES})
    list(APPEND NOISE_HEADERS_LIST ${WIN32_HEADERS})
endif()

# Add the static library
add_library(libnoise_static STATIC ${NOISE_SOURCES_LIST} ${NOISE_HEADERS_LIST})

if(${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
    target_compile_definitions(libnoise_static PRIVATE _GNU_SOURCE)
endif()

target_link_libraries(libnoise INTERFACE libnoise_static)
target_include_directories(libnoise INTERFACE ${SOURCE_DIR})

message(STATUS "libnoise using bundled version...")
