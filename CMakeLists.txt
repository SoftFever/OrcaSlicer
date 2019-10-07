project(PrusaSlicer)
cmake_minimum_required(VERSION 3.2)

include("version.inc")
include(GNUInstallDirs)

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

option(SLIC3R_STATIC 			"Compile PrusaSlicer with static libraries (Boost, TBB, glew)" ${SLIC3R_STATIC_INITIAL})
option(SLIC3R_GUI    			"Compile PrusaSlicer with GUI components (OpenGL, wxWidgets)" 1)
option(SLIC3R_FHS               "Assume PrusaSlicer is to be installed in a FHS directory structure" 0)
option(SLIC3R_WX_STABLE         "Build against wxWidgets stable (3.0) as oppsed to dev (3.1) on Linux" 0)
option(SLIC3R_PROFILE 			"Compile PrusaSlicer with an invasive Shiny profiler" 0)
option(SLIC3R_PCH               "Use precompiled headers" 1)
option(SLIC3R_MSVC_COMPILE_PARALLEL "Compile on Visual Studio in parallel" 1)
option(SLIC3R_MSVC_PDB          "Generate PDB files on MSVC in Release mode" 1)
option(SLIC3R_PERL_XS           "Compile XS Perl module and enable Perl unit and integration tests" 0)
option(SLIC3R_ASAN              "Enable ASan on Clang and GCC" 0)

set(SLIC3R_GTK "2" CACHE STRING "GTK version to use with wxWidgets on Linux")

# Proposal for C++ unit tests and sandboxes
option(SLIC3R_BUILD_SANDBOXES   "Build development sandboxes" OFF)
option(SLIC3R_BUILD_TESTS       "Build unit tests" ON)

# Print out the SLIC3R_* cache options
get_cmake_property(_cache_vars CACHE_VARIABLES)
list (SORT _cache_vars)
foreach (_cache_var ${_cache_vars})
    if("${_cache_var}" MATCHES "^SLIC3R_")
        message(STATUS "${_cache_var}: ${${_cache_var}}")
    endif ()
endforeach()

if (SLIC3R_GUI)
    add_definitions(-DSLIC3R_GUI)
endif ()

if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set(IS_CLANG_CL TRUE)

    # clang-cl can interpret SYSTEM header paths if -imsvc is used
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-imsvc")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall \
        -Wno-old-style-cast -Wno-reserved-id-macro -Wno-c++98-compat-pedantic")
else ()
    set(IS_CLANG_CL FALSE)
endif ()

if (MSVC)
    if (SLIC3R_MSVC_COMPILE_PARALLEL AND NOT IS_CLANG_CL)
           add_compile_options(/MP)
    endif ()
    # /bigobj (Increase Number of Sections in .Obj file)
    # error C3859: virtual memory range for PCH exceeded; please recompile with a command line option of '-Zm90' or greater
    # Generate symbols at every build target, even for the release.
    add_compile_options(-bigobj -Zm520 /Zi)
endif ()

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()

# Display and check CMAKE_PREFIX_PATH
message(STATUS "SLIC3R_STATIC: ${SLIC3R_STATIC}")
if (NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
    message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH} (from cache or command line)")
    set(PREFIX_PATH_CHECK ${CMAKE_PREFIX_PATH})
elseif (NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
    message(STATUS "CMAKE_PREFIX_PATH: $ENV{CMAKE_PREFIX_PATH} (from environment)")
    set(PREFIX_PATH_CHECK $ENV{CMAKE_PREFIX_PATH})
else ()
    message(STATUS "CMAKE_PREFIX_PATH: (default)")
endif ()

foreach (DIR ${PREFIX_PATH_CHECK})
    if (NOT EXISTS "${DIR}")
        message(WARNING "CMAKE_PREFIX_PATH element doesn't exist: ${DIR}")
    endif ()
endforeach ()

# Add our own cmake module path.
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules/)

enable_testing ()

# Enable C++11 language standard.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    # Add DEBUG flags to debug builds.
    add_compile_options("$<$<CONFIG:DEBUG>:-DDEBUG>")
endif()

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
    if(WIN10SDK_PATH)
        message("Building with Win10 Netfabb STL fixing service support")
        add_definitions(-DHAS_WIN10SDK)
        include_directories("${WIN10SDK_PATH}/Include")
    else()
        message("Building without Win10 Netfabb STL fixing service support")
    endif()
endif()

if (APPLE)
    message("OS X SDK Path: ${CMAKE_OSX_SYSROOT}")
    if (CMAKE_OSX_DEPLOYMENT_TARGET)
        message("OS X Deployment Target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else ()
        message("OS X Deployment Target: (default)")
    endif ()
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)

    if (CMAKE_VERSION VERSION_LESS "3.1")
        # Workaround for an old CMake, which does not understand CMAKE_CXX_STANDARD.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    endif()

    # Boost on Raspberry-Pi does not link to pthreads.
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUXX)
    # Adding -fext-numeric-literals to enable GCC extensions on definitions of quad float literals, which are required by Boost.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fext-numeric-literals" )
endif()

if (NOT MSVC AND ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
    if (NOT MINGW)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall" )
    endif ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-reorder" )

    # On GCC and Clang, no return from a non-void function is a warning only. Here, we make it an error.
    add_compile_options(-Werror=return-type)

    #removes LOTS of extraneous Eigen warnings (GCC only supports it since 6.1)
    #if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 6.1)
        # add_compile_options(-Wno-ignored-attributes) # Tamas: Eigen include dirs are marked as SYSTEM
    #endif()

    #GCC generates loads of -Wunknown-pragmas when compiling igl. The fix is not easy due to a bug in gcc, see
    # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66943 or
    # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53431
    # We will turn the warning of for GCC for now:
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options(-Wno-unknown-pragmas)
    endif()

    if (SLIC3R_ASAN)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")

        if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
        endif ()
    endif ()
endif()

if (APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")
endif ()

# Where all the bundled libraries reside?
set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(LIBDIR_BIN ${CMAKE_CURRENT_BINARY_DIR}/src)
# For the bundled boost libraries (boost::nowide)
include_directories(${LIBDIR})
# For generated header files
include_directories(${LIBDIR_BIN}/platform)
# For libslic3r.h
include_directories(${LIBDIR}/clipper ${LIBDIR}/polypartition)

if(WIN32)
    add_definitions(-D_USE_MATH_DEFINES -D_WIN32 -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS)
    if(MSVC)
        # BOOST_ALL_NO_LIB: Avoid the automatic linking of Boost libraries on Windows. Rather rely on explicit linking.
        add_definitions(-DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x601 )
    endif(MSVC)
endif(WIN32)

add_definitions(-DwxUSE_UNICODE -D_UNICODE -DUNICODE -DWXINTL_NO_GETTEXT_MACRO)

if (SLIC3R_PROFILE)
    message("PrusaSlicer will be built with a Shiny invasive profiler")
    add_definitions(-DSLIC3R_PROFILE)
endif ()

# Disable optimization even with debugging on.
if (0)
    message(STATUS "Perl compiled without optimization. Disabling optimization for the PrusaSlicer build.")
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
# set(Boost_COMPILER "-mgw81")
if(NOT WIN32)
    # boost::process was introduced first in version 1.64.0
    set(MINIMUM_BOOST_VERSION "1.64.0")
endif()
find_package(Boost ${MINIMUM_BOOST_VERSION} REQUIRED COMPONENTS system filesystem thread log locale regex)

add_library(boost_libs INTERFACE)
add_library(boost_headeronly INTERFACE)

if (APPLE)
    # BOOST_ASIO_DISABLE_KQUEUE : prevents a Boost ASIO bug on OS X: https://svn.boost.org/trac/boost/ticket/5339
    target_compile_definitions(boost_headeronly INTERFACE BOOST_ASIO_DISABLE_KQUEUE)
endif()

if(NOT SLIC3R_STATIC)
    target_compile_definitions(boost_headeronly INTERFACE BOOST_LOG_DYN_LINK)
endif()

if(TARGET Boost::system)
    message(STATUS "Boost::boost exists")
    target_link_libraries(boost_headeronly INTERFACE Boost::boost)
    target_link_libraries(boost_libs INTERFACE
        boost_headeronly # includes the custom compile definitions as well
        Boost::system
        Boost::filesystem
        Boost::thread
        Boost::log
        Boost::locale
        Boost::regex
        )
else()
    target_include_directories(boost_headeronly INTERFACE ${Boost_INCLUDE_DIRS})
    target_link_libraries(boost_libs INTERFACE boost_headeronly ${Boost_LIBRARIES})
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
    find_package(Eigen3 3.3)
endif ()
if (NOT EIGEN3_FOUND)
    set(EIGEN3_FOUND 1)
    set(EIGEN3_INCLUDE_DIR ${LIBDIR}/eigen/)
endif ()
include_directories(BEFORE SYSTEM ${EIGEN3_INCLUDE_DIR})

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

# Find the Cereal serialization library
add_library(cereal INTERFACE)
target_include_directories(cereal INTERFACE include)

# l10n
set(L10N_DIR "${SLIC3R_RESOURCES_DIR}/localization")
add_custom_target(pot
    COMMAND xgettext --keyword=L --keyword=L_CONTEXT:1,2c --keyword=_L_PLURAL:1,2 --add-comments=TRN --from-code=UTF-8 --debug
        -f "${L10N_DIR}/list.txt"
        -o "${L10N_DIR}/PrusaSlicer.pot"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generate pot file from strings in the source tree"
)

# libslic3r, PrusaSlicer GUI and the PrusaSlicer executable.
add_subdirectory(src)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT PrusaSlicer_app_console)

# Perl bindings, currently only used for the unit / integration tests of libslic3r.
# Also runs the unit / integration tests.
#FIXME Port the tests into C++ to finally get rid of the Perl!
if (SLIC3R_PERL_XS)
    add_subdirectory(xs)
endif ()

if(SLIC3R_BUILD_SANDBOXES)
    add_subdirectory(sandboxes)
endif()

if(SLIC3R_BUILD_TESTS)
    add_subdirectory(tests)
endif()


# Resources install target, configure fhs.hpp on UNIX
if (WIN32)
    install(DIRECTORY "${SLIC3R_RESOURCES_DIR}/" DESTINATION "${CMAKE_INSTALL_PREFIX}/resources")
elseif (SLIC3R_FHS)
    set(SLIC3R_FHS_RESOURCES "${CMAKE_INSTALL_FULL_DATAROOTDIR}/PrusaSlicer")
    install(DIRECTORY "${SLIC3R_RESOURCES_DIR}/" DESTINATION "${SLIC3R_FHS_RESOURCES}")
else ()
    install(DIRECTORY "${SLIC3R_RESOURCES_DIR}/" DESTINATION "${CMAKE_INSTALL_PREFIX}/resources")
endif ()
configure_file(${LIBDIR}/platform/unix/fhs.hpp.in ${LIBDIR_BIN}/platform/unix/fhs.hpp)
