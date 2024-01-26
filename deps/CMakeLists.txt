#
# This CMake project downloads, configures and builds OrcaSlicer dependencies on Unix and Windows.
#
# When using this script, it's recommended to perform an out-of-source build using CMake.
#
# All the dependencies are installed in a `destdir` directory in the root of the build directory,
# in a traditional Unix-style prefix structure. The destdir can be used directly by CMake
# when building OrcaSlicer - to do this, set the CMAKE_PREFIX_PATH to ${destdir}/usr/local.
# Warning: On UNIX/Linux, you also need to set -DSLIC3R_STATIC=1 when building OrcaSlicer.
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

cmake_minimum_required(VERSION 3.2)
if (APPLE)
    # if CMAKE_OSX_DEPLOYMENT_TARGET is not set, set it to 11.3
    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3" CACHE STRING "Minimum OS X deployment version" FORCE)
    endif ()
    message(STATUS "CMAKE_OSX_DEPLOYMENT_TARGET: ${CMAKE_OSX_DEPLOYMENT_TARGET}")

endif ()

project(OrcaSlicer-deps)

include(ExternalProject)
include(ProcessorCount)

ProcessorCount(NPROC)
if (NPROC EQUAL 0)
    set(NPROC 1)
endif ()

set(DESTDIR "${CMAKE_CURRENT_BINARY_DIR}/destdir" CACHE PATH "Destination directory")
set(DEP_DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}/../DL_CACHE CACHE PATH "Path for downloaded source packages.")

get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if (_is_multi)
    option(DEP_DEBUG "Build debug variants (only applicable on Windows)" OFF)
    option(ORCA_INCLUDE_DEBUG_INFO "Includes debug information in a release build (like RelWithDebInfo) in a way that works with multi-configuration generators and incompatible dependencies. DEP_DEBUG option takes priority over this." OFF)
endif ()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(DEP_WX_GTK3 "Build wxWidgets against GTK3" OFF)
else() 
    if(POLICY CMP0135) # DOWNLOAD_EXTRACT_TIMESTAMP
        cmake_policy(SET CMP0135 NEW)
    endif()
endif()

set(IS_CROSS_COMPILE FALSE)

if (APPLE)
    set(CMAKE_FIND_FRAMEWORK LAST)
    set(CMAKE_FIND_APPBUNDLE LAST)
    list(FIND CMAKE_OSX_ARCHITECTURES ${CMAKE_SYSTEM_PROCESSOR} _arch_idx)
    message(STATUS "prusaslicer_add_cmake_project for Apple")
    if (CMAKE_OSX_ARCHITECTURES AND _arch_idx LESS 0)
        message(STATUS "prusaslicer_add_cmake_project for Apple crosscompiling")
        set(IS_CROSS_COMPILE TRUE)
        set(CMAKE_CXX_COMPILER_ID "Clang")
        string(REPLACE ";" "$<SEMICOLON>" CMAKE_OSX_ARCHS "${CMAKE_OSX_ARCHITECTURES}")
        set(_cmake_osx_arch -DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHS})
        set(_cmake_args_osx_arch CMAKE_ARGS -DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHS})
        message(STATUS "Detect Cross-compilation. Will build for target ${CMAKE_OSX_ARCHS}" )
    endif ()
endif ()


# On developer machines, it can be enabled to speed up compilation and suppress warnings coming from IGL. 
# FIXME:
# Enabling this option is not safe. IGL will compile itself with its own version of Eigen while
# Slic3r compiles with a different version which will cause runtime errors.
# option(DEP_BUILD_IGL_STATIC "Build IGL as a static library. Might cause link errors and increase binary size." OFF)

message(STATUS "OrcaSlicer deps DESTDIR: ${DESTDIR}")
message(STATUS "OrcaSlicer dowload dir for source packages: ${DEP_DOWNLOAD_DIR}")
message(STATUS "OrcaSlicer deps debug build: ${DEP_DEBUG}")

find_package(Git REQUIRED)

# The default command line for patching. Only works for newer 
set(PATCH_CMD ${GIT_EXECUTABLE} apply --verbose --ignore-space-change --whitespace=fix)

if (NOT _is_multi AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "Forcing CMAKE_BUILD_TYPE to Release as it was not specified.")
endif ()

function(orcaslicer_add_cmake_project projectname)
    cmake_parse_arguments(P_ARGS "" "INSTALL_DIR;BUILD_COMMAND;INSTALL_COMMAND" "CMAKE_ARGS" ${ARGN})

    set(_configs_line -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE})
    if (_is_multi OR MSVC)
        if (ORCA_INCLUDE_DEBUG_INFO AND NOT DEP_DEBUG)
            set(_configs_line "-DCMAKE_C_FLAGS_RELEASE:STRING=${CMAKE_C_FLAGS_RELWITHDEBINFO} -DCMAKE_CXX_FLAGS_RELEASE:STRING=${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
        else ()
            set(_configs_line "")
        endif ()
    endif ()

    set(_gen "")
    set(_build_j "-j${NPROC}")
    if (MSVC)
        set(_gen CMAKE_GENERATOR "${DEP_MSVC_GEN}" CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}")
        set(_build_j "/m")
    endif ()

if (NOT IS_CROSS_COMPILE OR NOT APPLE)
    ExternalProject_Add(
        dep_${projectname}
        EXCLUDE_FROM_ALL    ON
        INSTALL_DIR         ${DESTDIR}/usr/local
        DOWNLOAD_DIR        ${DEP_DOWNLOAD_DIR}/${projectname}
        ${_gen}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:STRING=${DESTDIR}/usr/local
            -DCMAKE_MODULE_PATH:STRING=${PROJECT_SOURCE_DIR}/../cmake/modules
            -DCMAKE_PREFIX_PATH:STRING=${DESTDIR}/usr/local
            -DCMAKE_DEBUG_POSTFIX:STRING=d
            -DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}
            -DCMAKE_TOOLCHAIN_FILE:STRING=${CMAKE_TOOLCHAIN_FILE}
            -DBUILD_SHARED_LIBS:BOOL=OFF
            ${_cmake_osx_arch}
            "${_configs_line}"
            ${DEP_CMAKE_OPTS}
            ${P_ARGS_CMAKE_ARGS}
       ${P_ARGS_UNPARSED_ARGUMENTS}
       BUILD_COMMAND ${CMAKE_COMMAND} --build . --config Release -- ${_build_j}
       INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release
    )
else()
    ExternalProject_Add(
        dep_${projectname}
        EXCLUDE_FROM_ALL    ON
        INSTALL_DIR         ${DESTDIR}/usr/local
        DOWNLOAD_DIR        ${DEP_DOWNLOAD_DIR}/${projectname}
        ${_gen}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:STRING=${DESTDIR}/usr/local
            -DCMAKE_PREFIX_PATH:STRING=${DESTDIR}/usr/local
            -DBUILD_SHARED_LIBS:BOOL=OFF
            ${_cmake_osx_arch}
            "${_configs_line}"
            ${DEP_CMAKE_OPTS}
            ${P_ARGS_CMAKE_ARGS}
       ${P_ARGS_UNPARSED_ARGUMENTS}
       BUILD_COMMAND ${CMAKE_COMMAND} --build . --config Release -- ${_build_j}
       INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release
    )

endif()

endfunction(orcaslicer_add_cmake_project)


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

        message("OS X Deployment Target (inferred from SDK): ${DEP_OSX_TARGET}")
    endif ()

    include("deps-macos.cmake")
elseif (MINGW)
    message(STATUS "Building for MinGW...")
    include("deps-mingw.cmake")
else()
    include("deps-linux.cmake")
endif()

set(ZLIB_PKG "")
if (NOT ZLIB_FOUND) 
    include(ZLIB/ZLIB.cmake)
    set(ZLIB_PKG dep_ZLIB)
endif ()
set(PNG_PKG "")
if (NOT PNG_FOUND) 
    include(PNG/PNG.cmake)
    set(PNG_PKG dep_PNG)
endif ()
set(EXPAT_PKG "")
if (NOT EXPAT_FOUND) 
    include(EXPAT/EXPAT.cmake)
    set(EXPAT_PKG dep_EXPAT)
endif ()

set(DEP_Boost_COMPONENTS system iostreams filesystem thread log locale regex date_time)
include(Boost/Boost.cmake)

# The order of includes respects the dependencies between libraries
include(Cereal/Cereal.cmake)
include(Qhull/Qhull.cmake)
include(GLEW/GLEW.cmake)
include(GLFW/GLFW.cmake)
include(OpenCSG/OpenCSG.cmake)

include(TBB/TBB.cmake)

include(Blosc/Blosc.cmake)
include(OpenEXR/OpenEXR.cmake)
include(OpenVDB/OpenVDB.cmake)

include(GMP/GMP.cmake)
include(MPFR/MPFR.cmake)
include(CGAL/CGAL.cmake)

include(NLopt/NLopt.cmake)

include(OpenSSL/OpenSSL.cmake)

set(CURL_PKG "")
if (NOT CURL_FOUND)
    include(CURL/CURL.cmake)
    set(CURL_PKG dep_CURL)
endif ()

include(JPEG/JPEG.cmake)
include(TIFF/TIFF.cmake)
include(wxWidgets/wxWidgets.cmake)
include(OCCT/OCCT.cmake)
include(FREETYPE/FREETYPE.cmake)

set(_dep_list
    dep_Boost
    dep_TBB
    ${CURL_PKG}
    dep_wxWidgets
    dep_Cereal
    dep_NLopt
    dep_OpenVDB
    dep_OpenCSG
    dep_CGAL
    dep_OpenSSL
    dep_GLFW
    ${PNG_PKG}
    ${ZLIB_PKG}
    ${EXPAT_PKG}
    )

if (MSVC)
    # Experimental
    #list(APPEND _dep_list "dep_qhull")
else()
    list(APPEND _dep_list "dep_Qhull")
    # Not working, static build has different Eigen
    #list(APPEND _dep_list "dep_libigl")
endif()

list(APPEND _dep_list "dep_OCCT")
list(APPEND _dep_list "dep_FREETYPE")

add_custom_target(deps ALL DEPENDS ${_dep_list})

# Note: I'm not using any of the LOG_xxx options in ExternalProject_Add() commands
# because they seem to generate bogus build files (possibly a bug in ExternalProject).
