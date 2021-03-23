# https://cmake.org/cmake/help/latest/variable/MSVC_VERSION.html
if (MSVC_VERSION EQUAL 1800)
# 1800      = VS 12.0 (v120 toolset)
    set(DEP_VS_VER "12")
    set(DEP_BOOST_TOOLSET "msvc-12.0")
elseif (MSVC_VERSION EQUAL 1900)
# 1900      = VS 14.0 (v140 toolset)    
    set(DEP_VS_VER "14")
    set(DEP_BOOST_TOOLSET "msvc-14.0")
elseif (MSVC_VERSION LESS 1920)
# 1910-1919 = VS 15.0 (v141 toolset)
    set(DEP_VS_VER "15")
    set(DEP_BOOST_TOOLSET "msvc-14.1")
elseif (MSVC_VERSION LESS 1930)
# 1920-1929 = VS 16.0 (v142 toolset)
    set(DEP_VS_VER "16")
    set(DEP_BOOST_TOOLSET "msvc-14.2")
else ()
    message(FATAL_ERROR "Unsupported MSVC version")
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set(DEP_BOOST_TOOLSET "clang-win")
endif ()

if (${DEPS_BITS} EQUAL 32)
    set(DEP_MSVC_GEN "Visual Studio ${DEP_VS_VER}")
    set(DEP_PLATFORM "Win32")
else ()
    if (DEP_VS_VER LESS 16)
        set(DEP_MSVC_GEN "Visual Studio ${DEP_VS_VER} Win64")
    else ()
        set(DEP_MSVC_GEN "Visual Studio ${DEP_VS_VER}")
    endif ()
    set(DEP_PLATFORM "x64")
endif ()

if (${DEP_DEBUG})
    set(DEP_BOOST_DEBUG "debug")
else ()
    set(DEP_BOOST_DEBUG "")
endif ()

macro(add_debug_dep _dep)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(${_dep} BINARY_DIR)
    ExternalProject_Add_Step(${_dep} build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND msbuild /m /P:Configuration=Debug INSTALL.vcxproj
        WORKING_DIRECTORY "${BINARY_DIR}"
    )
endif ()
endmacro()

if (${DEPS_BITS} EQUAL 32)
    set(DEP_WXWIDGETS_TARGET "")
    set(DEP_WXWIDGETS_LIBDIR "vc_lib")
else ()
    set(DEP_WXWIDGETS_TARGET "TARGET_CPU=X64")
    set(DEP_WXWIDGETS_LIBDIR "vc_x64_lib")
endif ()

find_package(Git REQUIRED)
