# Boost 1.63 requires CMake 3.7 or newer
cmake_minimum_required(VERSION 2.8)

project(Slic3r)

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
set(ENV{PERL5LIB} "${PROJECT_SOURCE_DIR}/local-lib/lib/perl${ENV_PATH_SEPARATOR}$ENV{PERL5LIB}")
message("PATH: $ENV{PATH}")
message("PERL5LIB: $ENV{PERL5LIB}")
find_package(Perl REQUIRED)

# CMAKE_PREFIX_PATH is used to point CMake to the remaining dependencies (Boost, TBB, ...)
# We pick it from environment if it is not defined in another way
if(NOT DEFINED CMAKE_PREFIX_PATH)
	if(DEFINED ENV{CMAKE_PREFIX_PATH})
		set(CMAKE_PREFIX_PATH "$ENV{CMAKE_PREFIX_PATH}")
	endif()
endif()

enable_testing ()

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

install(PROGRAMS slic3r.pl DESTINATION bin RENAME slic3r-prusa3d)

file(GLOB MyVar var/*.png)
install(FILES ${MyVar} DESTINATION share/slic3r-prusa3d)
install(FILES lib/Slic3r.pm DESTINATION lib/slic3r-prusa3d)
install(DIRECTORY lib/Slic3r DESTINATION lib/slic3r-prusa3d)
