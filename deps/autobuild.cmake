if (CLEAN_DEPS)
    message(STATUS "Cleaning dependencies and rebuilding")
    file(REMOVE_RECURSE ${DEP_BUILD_DIR})
elseif (FORCE_DEPS)
    message(STATUS "Forcing rebuild of dependencies")
    set(_needs_build TRUE)
endif ()

find_package(Git REQUIRED QUIET)

# if there is already info about the last build, read it
if (EXISTS ${DEP_BUILD_DIR}/DEPS_BUILD_INFO.info)
    file(STRINGS ${DEP_BUILD_DIR}/DEPS_BUILD_INFO.info HASH_LIST)
    if (HASH_LIST)
        list(GET HASH_LIST 1 _parsed_last_commit)
        list(GET HASH_LIST 2 _parsed_last_uncommitted_md5)
    endif ()
else ()
    message(STATUS "No previous build info found")
endif ()

# get the diff of the dependencies folder
execute_process(
        COMMAND ${GIT_EXECUTABLE} --no-pager diff -w deps
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _cmd_deps_diff
        RESULT_VARIABLE _cmd_deps_diff_res
)

# get the hash of the last commit to the dependencies folder
execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --pretty=format:%H deps
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _cmd_last_commit
        RESULT_VARIABLE _cmd_last_commit_res
)

# check for errors while running the commands
if (NOT _cmd_deps_diff_res EQUAL 0)
    message(FATAL_ERROR "Failed to get status for deps")
elseif (NOT _cmd_last_commit_res EQUAL 0)
    message(FATAL_ERROR "Failed to get last commit date for deps")
endif ()

# check the results and determine if a build is needed
if (NOT _parsed_last_commit STREQUAL _cmd_last_commit)
    message(STATUS "Last commit for deps has changed")
    set(_needs_build TRUE)
endif ()
if (NOT _cmd_deps_diff STREQUAL "")
    string(MD5 _deps_diff_md5 "${_cmd_deps_diff}")
    if (NOT _parsed_last_uncommitted_md5 STREQUAL _deps_diff_md5)
        message(STATUS "Uncommitted changes made to deps folder")
        set(_needs_build TRUE)
    endif ()
endif ()

if (_needs_build)
    message(STATUS "Dependencies have been updated. Rebuilding")
elseif (NOT EXISTS ${DEP_BUILD_DIR})
    if (NOT CLEAN_DEPS)
        message(STATUS "Build directory for dependencies does not exist. Rebuilding")
    endif ()
else ()
    message(STATUS "Dependencies are up to date. Skipping build")
    return()
endif ()

set (_output_quiet "")
if (BUILD_DEPS_QUIET)
    set (_output_quiet OUTPUT_QUIET)
endif ()

set(_gen_arg "")
if (CMAKE_GENERATOR)
    set (_gen_arg "-G${CMAKE_GENERATOR}")
endif ()

set(_configure_args "")

if (CMAKE_C_COMPILER)
    list(APPEND _configure_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif ()

if (CMAKE_CXX_COMPILER)
    list(APPEND _configure_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif ()

if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND _configure_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif ()

set(_release_to_public "0")
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(_release_to_public "1")
endif ()

# Generic args
list(APPEND _configure_args "-DDESTDIR=${DEP_DESTDIR}" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

if (APPLE)
    list(APPEND _configure_args "-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}"
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "-DOPENSSL_ARCH=darwin64-${CMAKE_OSX_ARCHITECTURES}-cc"
    )
elseif (WIN32)
    list(APPEND _configure_args "-A x64")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _configure_args "-DDEP_DEBUG=ON")
    elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        list(APPEND _configure_args "-DORCA_INCLUDE_DEBUG_INFO=ON")
    endif ()
elseif (LINUX)
    if(NOT SLIC3R_GTK EQUAL "3")
        list(APPEND _configure_args "-DDEP_WX_GTK3=OFF")
    endif()
endif ()

message(STATUS "Configuring dependencies with the following command: ${CMAKE_COMMAND} ${_gen_arg} -B ${DEP_BUILD_DIR} ${_configure_args}")

execute_process(
        COMMAND ${CMAKE_COMMAND} "${_gen_arg}" -B ${DEP_BUILD_DIR} ${_configure_args}
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

# write current commit info to file
message(STATUS "Writing deps build status to ${DEP_BUILD_DIR}/DEPS_BUILD_INFO.info")
file(WRITE ${DEP_BUILD_DIR}/DEPS_BUILD_INFO.info
        "This file is used to determine if dependencies need to be rebuilt. Do not edit\n"
        "${_cmd_last_commit}\n"
        "${_deps_diff_md5}\n")