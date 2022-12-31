include(ExternalProject)

if (WIN32)
    set(_bootstrap_cmd bootstrap.bat)
    set(_build_cmd  b2.exe)
else()
    set(_bootstrap_cmd ./bootstrap.sh)
    set(_build_cmd ./b2)
endif()

set(_patch_command ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/common.jam ./tools/build/src/tools/common.jam)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    configure_file(${CMAKE_CURRENT_LIST_DIR}/user-config.jam boost-user-config.jam)
    set(_boost_toolset gcc)
    set(_patch_command ${_patch_command} && ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/boost-user-config.jam ./tools/build/src/tools/user-config.jam)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # https://cmake.org/cmake/help/latest/variable/MSVC_VERSION.html
    if (MSVC_VERSION EQUAL 1800)
    # 1800      = VS 12.0 (v120 toolset)
        set(_boost_toolset "msvc-12.0")
    elseif (MSVC_VERSION EQUAL 1900)
    # 1900      = VS 14.0 (v140 toolset)
        set(_boost_toolset "msvc-14.0")
    elseif (MSVC_VERSION LESS 1920)
    # 1910-1919 = VS 15.0 (v141 toolset)
        set(_boost_toolset "msvc-14.1")
    elseif (MSVC_VERSION LESS 1930)
    # 1920-1929 = VS 16.0 (v142 toolset)
        set(_boost_toolset "msvc-14.2")
    elseif (MSVC_VERSION LESS 1940)
    # 1930-1939 = VS 17.0 (v143 toolset)
        set(_boost_toolset "msvc-14.3")
    else ()
        message(FATAL_ERROR "Unsupported MSVC version")
    endif ()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if (WIN32)
        set(_boost_toolset "clang-win")
    else()
        set(_boost_toolset "clang")
    endif()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
    set(_boost_toolset "intel")
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(_boost_toolset "clang")
endif()

message(STATUS "Deduced boost toolset: ${_boost_toolset} based on ${CMAKE_CXX_COMPILER_ID} compiler")

set(_libs "")
foreach(_comp ${DEP_Boost_COMPONENTS})
    list(APPEND _libs "--with-${_comp}")
endforeach()

if (BUILD_SHARED_LIBS)
    set(_link shared)
else()
    set(_link static)
endif()

set(_bits "")
if ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
    set(_bits 64)
elseif ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
    set(_bits 32)
endif ()

include(ProcessorCount)
ProcessorCount(NPROC)
file(TO_NATIVE_PATH ${DESTDIR}/usr/local/ _prefix)

set(_boost_flags "")
if (UNIX) 
    set(_boost_flags "cflags=-fPIC;cxxflags=-fPIC")
elseif(APPLE)
    set(_boost_flags 
        "cflags=-fPIC -mmacosx-version-min=${DEP_OSX_TARGET};"
        "cxxflags=-fPIC -mmacosx-version-min=${DEP_OSX_TARGET};"
        "mflags=-fPIC -mmacosx-version-min=${DEP_OSX_TARGET};"
        "mmflags=-fPIC -mmacosx-version-min=${DEP_OSX_TARGET}") 
endif()

set(_boost_variants "")
if(CMAKE_BUILD_TYPE)
    list(APPEND CMAKE_CONFIGURATION_TYPES ${CMAKE_BUILD_TYPE})
    list(REMOVE_DUPLICATES CMAKE_CONFIGURATION_TYPES)
endif()
list(FIND CMAKE_CONFIGURATION_TYPES "Release" _cfg_rel)
list(FIND CMAKE_CONFIGURATION_TYPES "RelWithDebInfo" _cfg_relwdeb)
list(FIND CMAKE_CONFIGURATION_TYPES "MinSizeRel" _cfg_minsizerel)
list(FIND CMAKE_CONFIGURATION_TYPES "Debug" _cfg_deb)

if (_cfg_rel GREATER -1 OR _cfg_relwdeb GREATER -1 OR _cfg_minsizerel GREATER -1)
    list(APPEND _boost_variants release)
endif()

if (_cfg_deb GREATER -1 OR (MSVC AND ${DEP_DEBUG}) )
    list(APPEND _boost_variants debug)
endif()

if (NOT _boost_variants)
    set(_boost_variants release)
endif()

set(_build_cmd ${_build_cmd}
               ${_boost_flags}
               -j${NPROC}
               ${_libs}
               --layout=versioned
               --debug-configuration
               toolset=${_boost_toolset}
               address-model=${_bits}
               link=${_link}
               threading=multi
               boost.locale.icu=off
               --disable-icu
               ${_boost_variants}
               stage)

set(_install_cmd ${_build_cmd} --prefix=${_prefix} install)

ExternalProject_Add(
    dep_Boost
    URL "https://boostorg.jfrog.io/artifactory/main/release/1.75.0/source/boost_1_75_0.tar.gz"
    URL_HASH SHA256=aeb26f80e80945e82ee93e5939baebdca47b9dee80a07d3144be1e1a6a66dd6a
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/Boost
    CONFIGURE_COMMAND "${_bootstrap_cmd}"
    PATCH_COMMAND ${_patch_command}
    BUILD_COMMAND "${_build_cmd}"
    BUILD_IN_SOURCE    ON
    INSTALL_COMMAND "${_install_cmd}"
)

if ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
    # Patch the boost::polygon library with a custom one.
    ExternalProject_Add(dep_boost_polygon
        EXCLUDE_FROM_ALL ON
        # GIT_REPOSITORY "https://github.com/prusa3d/polygon"
        # GIT_TAG prusaslicer_gmp
        URL https://github.com/prusa3d/polygon/archive/refs/heads/prusaslicer_gmp.zip
        URL_HASH SHA256=abeb9710f0a7069fb9b22181ae5c56f6066002f125db210e7ffb27032aed6824
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/boost_polygon
        DEPENDS dep_Boost
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/dep_boost_polygon-prefix/src/dep_boost_polygon/include/boost/polygon"
            "${DESTDIR}/usr/local/include/boost/polygon"
    )
    # Only override boost::Polygon Voronoi implementation with Vojtech's GMP hacks on 64bit platforms.
    list(APPEND _dep_list "dep_boost_polygon")
endif ()