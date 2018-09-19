project(Slic3r)
cmake_minimum_required(VERSION 3.2)

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
option(SLIC3R_PRUSACONTROL		"Compile Slic3r with the PrusaControl prject file format (requires wxWidgets base library)" 1)
option(SLIC3R_PROFILE 			"Compile Slic3r with an invasive Shiny profiler" 0)
option(SLIC3R_MSVC_COMPILE_PARALLEL "Compile on Visual Studio in parallel" 1)
option(SLIC3R_MSVC_PDB          "Generate PDB files on MSVC in Release mode" 1)

if (MSVC AND SLIC3R_MSVC_COMPILE_PARALLEL)
	add_compile_options(/MP)
endif ()

# Find the Perl interpreter, add local-lib to PATH and PERL5LIB environment variables,
# so the locally installed modules (mainly the Alien::wxPerl) will be reached.
if (WIN32)
    set(ENV_PATH_SEPARATOR ";")
else()
    set(ENV_PATH_SEPARATOR ":")
endif()
set(ENV{PATH}     "${PROJECT_SOURCE_DIR}/local-lib/bin${ENV_PATH_SEPARATOR}$ENV{PATH}")
set(PERL_INCLUDE  "${PROJECT_SOURCE_DIR}/local-lib/lib/perl5${ENV_PATH_SEPARATOR}$ENV{PERL5LIB}")
message("PATH: $ENV{PATH}")
message("PERL_INCLUDE: ${PERL_INCLUDE}")
find_package(Perl REQUIRED)
if (WIN32)
    # On Windows passing the PERL5LIB variable causes various problems (such as with MAX_PATH and others),
    # basically I've found no good way to do it on Windows.
    set(PERL5LIB_ENV_CMD "")
else()
    set(PERL5LIB_ENV_CMD ${CMAKE_COMMAND} -E env PERL5LIB=${PERL_INCLUDE})
endif()

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

# Enable C11 language standard.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

# Where all the bundled libraries reside?
set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/)
# For the bundled boost libraries (boost::nowide)
include_directories(${LIBDIR})
# For libslic3r.h
include_directories(${LIBDIR}/libslic3r ${LIBDIR}/clipper ${LIBDIR}/polypartition)
#set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(WIN32)
    # BOOST_ALL_NO_LIB: Avoid the automatic linking of Boost libraries on Windows. Rather rely on explicit linking.
    add_definitions(-D_USE_MATH_DEFINES -D_WIN32 -DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x601)
endif()

add_definitions(-DwxUSE_UNICODE -D_UNICODE -DUNICODE -DWXINTL_NO_GETTEXT_MACRO)

if (SLIC3R_PROFILE)
    message("Slic3r will be built with a Shiny invasive profiler")
    add_definitions(-DSLIC3R_PROFILE)
endif ()

# Perl specific stuff
find_package(PerlLibs REQUIRED)
set(PerlEmbed_DEBUG 1)
find_package(PerlEmbed REQUIRED)
# If the Perl is compiled with optimization off, disable optimization over the whole project.
if (WIN32 AND ";${PerlEmbed_CCFLAGS};" MATCHES ";[-/]Od;")
    message(STATUS "Perl compiled without optimization. Disabling optimization for the Slic3r build.")
    message("Old CMAKE_CXX_FLAGS_RELEASE: ${CMAKE_CXX_FLAGS_RELEASE}")
    message("Old CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELEASE}")
    message("Old CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS_RELEASE}")
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /Od /Zi /EHsc /DNDEBUG /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_C_FLAGS_RELEASE "/MD /Od /Zi /DNDEBUG /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MD /Od /Zi /EHsc /DNDEBUG /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/MD /Od /Zi /DNDEBUG /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_CXX_FLAGS "/MD /Od /Zi /EHsc /DNDEBUG /DWIN32 /DTBB_USE_ASSERT")
    set(CMAKE_C_FLAGS "/MD /Od /Zi /DNDEBUG /DWIN32 /DTBB_USE_ASSERT")
endif()
# The following line will add -fPIC on Linux to make the XS.so rellocable.
add_definitions(${PerlEmbed_CCCDLFLAGS})

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

# Find and configure wxWidgets
if (SLIC3R_PRUSACONTROL)
    set(wxWidgets_UseAlienWx 1)
    if (wxWidgets_UseAlienWx)
        set(AlienWx_DEBUG 1)
        find_package(AlienWx REQUIRED COMPONENTS base core adv html gl)
        include_directories(${AlienWx_INCLUDE_DIRS})
        #add_compile_options(${AlienWx_CXX_FLAGS})
        add_definitions(${AlienWx_DEFINITIONS})
        set(wxWidgets_LIBRARIES ${AlienWx_LIBRARIES})
        # On Linux / gtk, we need to have a direct access to gtk+ for some workarounds.
        if (AlienWx_GUI_TOOLKIT STREQUAL "gtk2")
            pkg_check_modules(GTK2 gtk+-2.0)
            include_directories(${GTK2_INCLUDE_DIRS})
        endif()
        if (AlienWx_GUI_TOOLKIT STREQUAL "gtk3")
            pkg_check_modules(GTK3 gtk+-3.0)
            include_directories(${GTK3_INCLUDE_DIRS})
        endif()
    else ()
        find_package(wxWidgets REQUIRED COMPONENTS base core adv html gl)
        include(${wxWidgets_USE_FILE})
    endif ()
#FIXME rewrite the PRUS format to miniz!
#    add_definitions(-DSLIC3R_GUI -DSLIC3R_PRUS)
endif()

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
add_subdirectory(resources/localization)

# libslic3r, Slic3r GUI and the slic3r executable.
add_subdirectory(src)

# Perl bindings, currently only used for the unit / integration tests of libslic3r.
add_subdirectory(xs)

get_filename_component(PERL_BIN_PATH "${PERL_EXECUTABLE}" DIRECTORY)
if (MSVC)
	# By default the startup project in MSVC is the 'ALL_BUILD' cmake-created project,
	# but we want 'slic3r' as the startup one because debugging run command is associated with it.
	# (Unfortunatelly it cannot be associated with ALL_BUILD using CMake.)
	# Note: For some reason this needs to be set in the top-level CMakeLists.txt
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT XS)
	set(PERL_PROVE "${PERL_BIN_PATH}/prove.bat")
else ()
	set(PERL_PROVE "${PERL_BIN_PATH}/prove")
endif ()

add_test (NAME xs COMMAND "${PERL_EXECUTABLE}" ${PERL_PROVE} -I ${PROJECT_SOURCE_DIR}/local-lib/lib/perl5 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/xs)
add_test (NAME integration COMMAND "${PERL_EXECUTABLE}" ${PERL_PROVE} WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

#install(PROGRAMS slic3r.pl DESTINATION bin RENAME slic3r-prusa3d)

file(GLOB MyVar var/*.png)
install(FILES ${MyVar} DESTINATION share/slic3r-prusa3d)
install(FILES lib/Slic3r.pm DESTINATION lib/slic3r-prusa3d)
install(DIRECTORY lib/Slic3r DESTINATION lib/slic3r-prusa3d)
