#
# This CMake project downloads, configures and builds Slic3r PE dependencies on Unix and Windows.
#
# When using this script, it's recommended to perform an out-of-source build using CMake.
#
# All the dependencies are installed in a `destdir` directory in the root of the build directory,
# in a traditional Unix-style prefix structure. The destdir can be used directly by CMake
# when building Slic3r - to do this, set the CMAKE_PREFIX_PATH to ${destdir}/usr/local.
#
# For better clarity of console output, it's recommended to _not_ use a parallelized build
# for the top-level command, ie. use `make -j 1` or `ninja -j 1` to force single-threaded top-level
# build. This doesn't degrade performance as individual dependencies are built in parallel fashion
# if supported by the dependency.
#
# On Windows, architecture (64 vs 32 bits) is judged based on the compiler variant.
# To build dependencies for either 64 or 32 bit OS, use the respective compiler command line.
#

project(Slic3r-deps)
cmake_minimum_required(VERSION 3.2)

include(ExternalProject)
include(ProcessorCount)

ProcessorCount(NPROC)
if (NPROC EQUAL 0)
    set(NPROC 1)
endif ()

set(DESTDIR "${CMAKE_CURRENT_BINARY_DIR}/destdir" CACHE PATH "Destination directory")
option(DEP_DEBUG "Build debug variants (only applicable on Windows)" ON)

message(STATUS "Slic3r deps DESTDIR: ${DESTDIR}")
message(STATUS "Slic3r deps debug build: ${DEP_DEBUG}")

if (MSVC)
    if ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
        message(STATUS "\nDetected 64-bit compiler => building 64-bit deps bundle\n")
        set(DEPS_BITS 64)
        include("deps-windows.cmake")
    elseif ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
        message(STATUS "\nDetected 32-bit compiler => building 32-bit deps bundle\n")
        set(DEPS_BITS 32)
        include("deps-windows.cmake")
    else ()
        message(FATAL_ERROR "Unable to detect architecture")
    endif ()
elseif (APPLE)
    set(DEPS_OSX_TARGET "10.9" CACHE STRING "OS X SDK version to build against")
    set(DEPS_OSX_SYSROOT
        "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX${DEPS_OSX_TARGET}.sdk"
        CACHE PATH "OS X SDK directory"
    )

    include("deps-macos.cmake")
else ()
    include("deps-linux.cmake")
endif()

add_custom_target(deps ALL
    DEPENDS
        dep_boost
        dep_tbb
        dep_libcurl
        dep_wxwidgets
        dep_gtest
        dep_nlopt
        dep_libpng
)

# Note: I'm not using any of the LOG_xxx options in ExternalProject_Add() commands
# because they seem to generate bogus build files (possibly a bug in ExternalProject).
