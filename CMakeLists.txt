project(Slic3r)
cmake_minimum_required(VERSION 3.2)

include("version.inc")
set(SLIC3R_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
file(TO_NATIVE_PATH "${SLIC3R_RESOURCES_DIR}" SLIC3R_RESOURCES_DIR_WIN)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (default Release)" FORCE)
endif()

if(DEFINED ENV{SLIC3R_STATIC})
	set(SLIC3R_STATIC_INITIAL $ENV{SLIC3R_STATIC})
else()
	if (MSVC OR MINGW OR APPLE)
		set(SLIC3R_STATIC_INITIAL 1)
	else()
		set(SLIC3R_STATIC_INITIAL 0)
	endif()
endif()

option(SLIC3R_STATIC 			"Compile Slic3r with static libraries (Boost, TBB, glew)" ${SLIC3R_STATIC_INITIAL})
option(SLIC3R_GUI    			"Compile Slic3r with GUI components (OpenGL, wxWidgets)" 1)
option(SLIC3R_PROFILE 			"Compile Slic3r with an invasive Shiny profiler" 0)
option(SLIC3R_MSVC_COMPILE_PARALLEL "Compile on Visual Studio in parallel" 1)
option(SLIC3R_MSVC_PDB          "Generate PDB files on MSVC in Release mode" 1)

if (MSVC AND SLIC3R_MSVC_COMPILE_PARALLEL)
	add_compile_options(/MP)
endif ()

# CMAKE_PREFIX_PATH is used to point CMake to the remaining dependencies (Boost, TBB, ...)
# We pick it from environment if it is not defined in another way
if(NOT DEFINED CMAKE_PREFIX_PATH)
	if(DEFINED ENV{CMAKE_PREFIX_PATH})
		set(CMAKE_PREFIX_PATH "$ENV{CMAKE_PREFIX_PATH}")
	endif()
endif()

# Add our own cmake module path.
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules/)

enable_testing ()

# Enable C++11 language standard.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# To be able to link libslic3r with the Perl XS module.
# Once we get rid of Perl and libslic3r is linked statically, we can get rid of -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# WIN10SDK_PATH is used to point CMake to the WIN10 SDK installation directory.
# We pick it from environment if it is not defined in another way
if(WIN32)
	if(NOT DEFINED WIN10SDK_PATH)
		if(DEFINED ENV{WIN10SDK_PATH})
			set(WIN10SDK_PATH "$ENV{WIN10SDK_PATH}")
		endif()
	endif()
	if(DEFINED WIN10SDK_PATH AND NOT EXISTS "${WIN10SDK_PATH}/include/winrt/windows.graphics.printing3d.h")
		message("WIN10SDK_PATH is invalid: ${WIN10SDK_PATH}")
		message("${WIN10SDK_PATH}/include/winrt/windows.graphics.printing3d.h was not found")
		message("STL fixing by the Netfabb service will not be compiled")
		unset(WIN10SDK_PATH)
	endif()
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Workaround for an old CMake, which does not understand CMAKE_CXX_STANDARD.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wno-reorder" )
    find_package(PkgConfig REQUIRED)
endif()

if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUXX)
    # Adding -fext-numeric-literals to enable GCC extensions on definitions of quad float literals, which are required by Boost.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fext-numeric-literals" )
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # On GCC and Clang, no return from a non-void function is a warning only. Here, we make it an error.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type" )
endif()

# Where all the bundled libraries reside?
set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/)
# For the bundled boost libraries (boost::nowide)
include_directories(${LIBDIR})
# For libslic3r.h
include_directories(${LIBDIR}/libslic3r ${LIBDIR}/clipper ${LIBDIR}/polypartition)
#set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(WIN32)
    # BOOST_ALL_NO_LIB: Avoid the automatic linking of Boost libraries on Windows. Rather rely on explicit linking.
    add_definitions(-D_USE_MATH_DEFINES -D_WIN32 -DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x601 -D_CRT_SECURE_NO_WARNINGS)
endif()

add_definitions(-DwxUSE_UNICODE -D_UNICODE -DUNICODE -DWXINTL_NO_GETTEXT_MACRO)

if (SLIC3R_PROFILE)
    message("Slic3r will be built with a Shiny invasive profiler")
    add_definitions(-DSLIC3R_PROFILE)
endif ()

# Disable optimization even with debugging on.
if (0)
    message(STATUS "Perl compiled without optimization. Disabling optimization for the Slic3r build.")
    message("Old CMAKE_CXX_FLAGS_RELEASE: ${CMAKE_CXX_FLAGS_RELEASE}")
    message("Old CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELEASE}")
    message("Old CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS_RELEASE}")
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /Od /Zi /EHsc /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_C_FLAGS_RELEASE "/MD /Od /Zi /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MD /Od /Zi /EHsc /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/MD /Od /Zi /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_CXX_FLAGS "/MD /Od /Zi /EHsc /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_C_FLAGS "/MD /Od /Zi /DWIN32 /DTBB_USE_ASSERT")
endif()

# Find and configure boost
if(SLIC3R_STATIC)
    # Use static boost libraries.
    set(Boost_USE_STATIC_LIBS ON)
    # Use boost libraries linked statically to the C++ runtime.
    # set(Boost_USE_STATIC_RUNTIME ON)
endif()
#set(Boost_DEBUG ON)
find_package(Boost REQUIRED COMPONENTS system filesystem thread log locale regex)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    if (APPLE)
        # BOOST_ASIO_DISABLE_KQUEUE : prevents a Boost ASIO bug on OS X: https://svn.boost.org/trac/boost/ticket/5339
        add_definitions(-DBOOST_ASIO_DISABLE_KQUEUE)
    endif()
    if(NOT SLIC3R_STATIC)
        add_definitions(-DBOOST_LOG_DYN_LINK)
    endif()
endif()

# Find and configure intel-tbb
if(SLIC3R_STATIC)
    set(TBB_STATIC 1)
endif()
set(TBB_DEBUG 1)
find_package(TBB REQUIRED)
include_directories(${TBB_INCLUDE_DIRS})
add_definitions(${TBB_DEFINITIONS})
if(MSVC)
    # Suppress implicit linking of the TBB libraries by the Visual Studio compiler.
    add_definitions(-D__TBB_NO_IMPLICIT_LINKAGE)
endif()
# The Intel TBB library will use the std::exception_ptr feature of C++11.
add_definitions(-DTBB_USE_CAPTURED_EXCEPTION=0)

find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})

if (SLIC3R_STATIC)
    if (NOT APPLE)
        # libcurl is always linked dynamically to the system libcurl on OSX.
        # On other systems, libcurl is linked statically if SLIC3R_STATIC is set.
        add_definitions(-DCURL_STATICLIB)
    endif()
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # As of now, our build system produces a statically linked libcurl,
        # which links the OpenSSL library dynamically.
        find_package(OpenSSL REQUIRED)
        message("OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
        message("OpenSSL libraries: ${OPENSSL_LIBRARIES}")
        include_directories(${OPENSSL_INCLUDE_DIR})
    endif()
endif()

## OPTIONAL packages

# Find eigen3 or use bundled version
if (NOT SLIC3R_STATIC)
    find_package(Eigen3)
endif ()
if (NOT Eigen3_FOUND)
    set(Eigen3_FOUND 1)
    set(EIGEN3_INCLUDE_DIR ${LIBDIR}/eigen/)
endif ()
include_directories(${EIGEN3_INCLUDE_DIR})

# Find expat or use bundled version
# Always use the system libexpat on Linux.
if (NOT SLIC3R_STATIC OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(EXPAT)
endif ()
if (NOT EXPAT_FOUND)
    add_library(expat STATIC
        ${LIBDIR}/expat/xmlparse.c
        ${LIBDIR}/expat/xmlrole.c
        ${LIBDIR}/expat/xmltok.c
    )
    set(EXPAT_FOUND 1)
    set(EXPAT_INCLUDE_DIRS ${LIBDIR}/expat/)
    set(EXPAT_LIBRARIES expat)
endif ()
include_directories(${EXPAT_INCLUDE_DIRS})

# Find glew or use bundled version
if (NOT SLIC3R_STATIC)
    find_package(GLEW)
endif ()
if (NOT GLEW_FOUND)
    add_library(glew STATIC ${LIBDIR}/glew/src/glew.c)
    set(GLEW_FOUND 1)
    set(GLEW_INCLUDE_DIRS ${LIBDIR}/glew/include/)
    set(GLEW_LIBRARIES glew)
    add_definitions(-DGLEW_STATIC)
endif ()
include_directories(${GLEW_INCLUDE_DIRS})

# l10n
set(L10N_DIR "${SLIC3R_RESOURCES_DIR}/localization")
add_custom_target(pot
    # FIXME: file list stale
    COMMAND xgettext --keyword=L --from-code=UTF-8 --debug
        -f "${L10N_DIR}/list.txt"
        -o "${L10N_DIR}/Slic3rPE.pot"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generate pot file from strings in the source tree"
)

# libslic3r, Slic3r GUI and the slic3r executable.
add_subdirectory(src)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT slic3r)

# Perl bindings, currently only used for the unit / integration tests of libslic3r.
# Also runs the unit / integration tests.
#FIXME Port the tests into C++ to finally get rid of the Perl!
add_subdirectory(xs)

file(GLOB MyVar var/*.png)
install(FILES ${MyVar} DESTINATION share/slic3r-prusa3d)
