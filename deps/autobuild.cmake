# TODO: Create a hash of the dependency folders and check if they have been updated

if (CLEAN_DEPS)
    message(STATUS "Cleaning dependencies")
    file(REMOVE_RECURSE ${DEP_BUILD_DIR})
endif ()

set (_output_quiet "")
if (BUILD_DEPS_QUIET)
    set (_output_quiet OUTPUT_QUIET)
endif ()

set(_gen_arg "")
if (CMAKE_GENERATOR)
    set (_gen_arg "-G${CMAKE_GENERATOR}")
endif ()

set(_build_args "")

if (CMAKE_C_COMPILER)
    list(APPEND _build_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif ()

if (CMAKE_CXX_COMPILER)
    list(APPEND _build_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif ()

if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND _build_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif ()

set(_release_to_public "0")
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(_release_to_public "1")
endif ()

# Generic args
list(APPEND _build_args "-DDESTDIR=${DEP_DESTDIR}" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" "-DBBL_RELEASE_TO_PUBLIC=${_release_to_public}")

if (APPLE)
    list(APPEND _build_args "-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}"
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "-DOPENSSL_ARCH=darwin64-${CMAKE_OSX_ARCHITECTURES}-cc"
    )
elseif (WIN32)
    list(APPEND _build_args "-A x64")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _build_args "-DDEP_DEBUG=ON")
    elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        list(APPEND _build_args "-DORCA_INCLUDE_DEBUG_INFO=ON")
    endif ()
else ()
    if(NOT SLIC3R_GTK EQUAL "3")
        list(APPEND _build_args "-DDEP_WX_GTK3=OFF")
    endif()
endif ()

message(STATUS "Configuring dependencies with the following command: ${CMAKE_COMMAND} ${_gen_arg} -B ${DEP_BUILD_DIR} ${_build_args}")

execute_process(
        COMMAND ${CMAKE_COMMAND} "${_gen_arg}" -B ${DEP_BUILD_DIR} ${_build_args}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        ${_output_quiet}
        ERROR_VARIABLE _deps_configure_output
        RESULT_VARIABLE _deps_configure_result
)

if (NOT _deps_configure_result EQUAL 0)
    message(FATAL_ERROR "Dependency configure failed with output:\n${_deps_configure_output}")
else ()
    message(STATUS "Building dependencies with the following command: ${CMAKE_COMMAND} --build . --target deps --config ${CMAKE_BUILD_TYPE} -- ${_compiler_args}")
    execute_process(
            COMMAND ${CMAKE_COMMAND} --build . --target deps --config ${CMAKE_BUILD_TYPE} -- ${_compiler_args}
            WORKING_DIRECTORY ${DEP_BUILD_DIR}
            ${_output_quiet}
            ERROR_VARIABLE _deps_build_output
            RESULT_VARIABLE _deps_build_result
    )
    if (NOT _deps_build_result EQUAL 0)
        message(FATAL_ERROR "Dependency build failed with output:\n${_deps_build_output}")
    else ()
        message(STATUS "Dependencies built successfully")
    endif ()
endif ()

#list(APPEND CMAKE_PREFIX_PATH ${_build_dir}/destdir/usr/local)
#set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING "")