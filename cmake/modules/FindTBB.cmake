# This is a wrapper of FindTBB which prefers the config scripts if available in the system
# but only if building with dynamic dependencies. The config scripts potentially belong
# to TBB >= 2020 which is incompatible with OpenVDB in our static dependency bundle.
# This workaround is useful for package maintainers on Linux systems to use newer versions
# of intel TBB (renamed to oneTBB from version 2021 up).
set(_q "")
if(${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
    set(_q QUIET)
endif()

# Only consider the config scripts if not building with the static dependencies
# and this call is not made from a static dependency build (e.g. dep_OpenVDB will use this module)
# BUILD_SHARED_LIBS will always be defined for dependency projects and will be OFF.
# Newer versions of TBB also discourage from using TBB as a static library
if (NOT SLIC3R_STATIC AND (NOT DEFINED BUILD_SHARED_LIBS OR BUILD_SHARED_LIBS)) 
    find_package(${CMAKE_FIND_PACKAGE_NAME} ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} CONFIG ${_q})

    if(NOT ${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
        if (NOT ${CMAKE_FIND_PACKAGE_NAME}_FOUND)
            message(STATUS "Falling back to MODULE search for ${CMAKE_FIND_PACKAGE_NAME}...")
        else()
            message(STATUS "${CMAKE_FIND_PACKAGE_NAME} found in ${${CMAKE_FIND_PACKAGE_NAME}_DIR}")
        endif()
    endif()

endif ()

if (NOT ${CMAKE_FIND_PACKAGE_NAME}_FOUND)
    include(${CMAKE_CURRENT_LIST_DIR}/FindTBB.cmake.in)
endif ()
