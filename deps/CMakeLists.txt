#
# This CMake project downloads, configures and builds PrusaSlicer dependencies on Unix and Windows.
#
# When using this script, it's recommended to perform an out-of-source build using CMake.
#
# All the dependencies are installed in a `destdir` directory in the root of the build directory,
# in a traditional Unix-style prefix structure. The destdir can be used directly by CMake
# when building PrusaSlicer - to do this, set the CMAKE_PREFIX_PATH to ${destdir}/usr/local.
# Warning: On UNIX/Linux, you also need to set -DSLIC3R_STATIC=1 when building PrusaSlicer.
#
# For better clarity of console output, it's recommended to _not_ use a parallelized build
# for the top-level command, ie. use `make -j 1` or `ninja -j 1` to force single-threaded top-level
# build. This doesn't degrade performance as individual dependencies are built in parallel fashion
# if supported by the dependency.
#
# On Windows, architecture (64 vs 32 bits) is judged based on the compiler variant.
# To build dependencies for either 64 or 32 bit OS, use the respective compiler command line.
#
# WARNING: On UNIX platforms wxWidgets hardcode the destdir path into its `wx-conffig` utility,
# therefore, unfortunatelly, the installation cannot be copied/moved elsewhere without re-installing wxWidgets.
#

project(PrusaSlicer-deps)
cmake_minimum_required(VERSION 3.2)

include(ExternalProject)
include(ProcessorCount)

ProcessorCount(NPROC)
if (NPROC EQUAL 0)
    set(NPROC 1)
endif ()

set(DESTDIR "${CMAKE_CURRENT_BINARY_DIR}/destdir" CACHE PATH "Destination directory")

option(DEP_DEBUG "Build debug variants (only applicable on Windows)" ON)
option(DEP_WX_STABLE "Build against wxWidgets stable 3.0 as opposed to default 3.1 (Linux only)" OFF)

# On developer machines, it can be enabled to speed up compilation and suppress warnings coming from IGL. 
# FIXME:
# Enabling this option is not safe. IGL will compile itself with its own version of Eigen while
# Slic3r compiles with a different version which will cause runtime errors.
# option(DEP_BUILD_IGL_STATIC "Build IGL as a static library. Might cause link errors and increase binary size." OFF)

message(STATUS "PrusaSlicer deps DESTDIR: ${DESTDIR}")
message(STATUS "PrusaSlicer deps debug build: ${DEP_DEBUG}")

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
    message("OS X SDK Path: ${CMAKE_OSX_SYSROOT}")
    if (CMAKE_OSX_DEPLOYMENT_TARGET)
        set(DEP_OSX_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        message("OS X Deployment Target: ${DEP_OSX_TARGET}")
    else ()
        # Attempt to infer the SDK version from the CMAKE_OSX_SYSROOT,
        # this is done because wxWidgets need the min version explicitly set
        string(REGEX MATCH "[0-9]+[.][0-9]+[.]sdk$" DEP_OSX_TARGET "${CMAKE_OSX_SYSROOT}")
        string(REGEX MATCH "^[0-9]+[.][0-9]+" DEP_OSX_TARGET "${DEP_OSX_TARGET}")

        if (NOT DEP_OSX_TARGET)
            message(FATAL_ERROR "Could not determine OS X SDK version. Please use -DCMAKE_OSX_DEPLOYMENT_TARGET=<version>")
        endif ()

        message("OS X Deployment Target (inferred from default): ${DEP_OSX_TARGET}")
    endif ()

    include("deps-macos.cmake")
else ()
    include("deps-linux.cmake")
endif()

if (MSVC)

    add_custom_target(deps ALL
    DEPENDS
        dep_boost
        dep_tbb
        dep_libcurl
        dep_wxwidgets
        dep_gtest
        dep_nlopt
        # dep_qhull # Experimental
        dep_zlib    # on Windows we still need zlib
    )

else()

    add_custom_target(deps ALL
    DEPENDS
        dep_boost
        dep_tbb
        dep_libcurl
        dep_wxwidgets
        dep_gtest
        dep_nlopt
        dep_qhull
        # dep_libigl # Not working, static build has different Eigen
    )

endif()

# Note: I'm not using any of the LOG_xxx options in ExternalProject_Add() commands
# because they seem to generate bogus build files (possibly a bug in ExternalProject).
