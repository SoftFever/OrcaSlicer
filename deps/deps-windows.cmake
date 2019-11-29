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

ExternalProject_Add(dep_boost
    EXCLUDE_FROM_ALL 1
    URL "https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz"
    URL_HASH SHA256=882b48708d211a5f48e60b0124cf5863c1534cd544ecd0664bb534a4b5d506e9
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND bootstrap.bat
    BUILD_COMMAND b2.exe
        -j "${NPROC}"
        --with-system
        --with-iostreams
        --with-filesystem
        --with-thread
        --with-log
        --with-locale
        --with-regex
        "--prefix=${DESTDIR}/usr/local"
        "address-model=${DEPS_BITS}"
        "toolset=${DEP_BOOST_TOOLSET}"
        link=static
        variant=release
        threading=multi
        boost.locale.icu=off
        "${DEP_BOOST_DEBUG}" release install
    INSTALL_COMMAND ""   # b2 does that already
)

ExternalProject_Add(dep_tbb
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/wjakob/tbb/archive/a0dc9bf76d0120f917b641ed095360448cabc85b.tar.gz"
    URL_HASH SHA256=0545cb6033bd1873fcae3ea304def720a380a88292726943ae3b9b207f322efe
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}"
    CMAKE_ARGS
        -DCMAKE_DEBUG_POSTFIX=_debug
        -DTBB_BUILD_SHARED=OFF
        -DTBB_BUILD_TESTS=OFF
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /m /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)

add_debug_dep(dep_tbb)

# ExternalProject_Add(dep_gtest
#     EXCLUDE_FROM_ALL 1
#     URL "https://github.com/google/googletest/archive/release-1.8.1.tar.gz"
#     URL_HASH SHA256=9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c
#     CMAKE_GENERATOR "${DEP_MSVC_GEN}"
#     CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}"
#     CMAKE_ARGS
#         -DBUILD_GMOCK=OFF
#         -Dgtest_force_shared_crt=ON
#         -DCMAKE_POSITION_INDEPENDENT_CODE=ON
#         "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
#     BUILD_COMMAND msbuild /m /P:Configuration=Release INSTALL.vcxproj
#     INSTALL_COMMAND ""
# )

# add_debug_dep(dep_gtest)

ExternalProject_Add(dep_cereal
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/USCiLab/cereal/archive/v1.2.2.tar.gz"
#    URL_HASH SHA256=c6dd7a5701fff8ad5ebb45a3dc8e757e61d52658de3918e38bab233e7fd3b4ae
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}"
    CMAKE_ARGS
        -DJUST_INSTALL_CEREAL=on
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /m /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)

ExternalProject_Add(dep_nlopt
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/stevengj/nlopt/archive/v2.5.0.tar.gz"
    URL_HASH SHA256=c6dd7a5701fff8ad5ebb45a3dc8e757e61d52658de3918e38bab233e7fd3b4ae
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}"
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DNLOPT_PYTHON=OFF
        -DNLOPT_OCTAVE=OFF
        -DNLOPT_MATLAB=OFF
        -DNLOPT_GUILE=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=d
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /m /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)

add_debug_dep(dep_nlopt)

ExternalProject_Add(dep_zlib
    EXCLUDE_FROM_ALL 1
    URL "https://zlib.net/zlib-1.2.11.tar.xz"
    URL_HASH SHA256=4ff941449631ace0d4d203e3483be9dbc9da454084111f97ea0a2114e19bf066
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}"
    CMAKE_ARGS
        -DSKIP_INSTALL_FILES=ON                                    # Prevent installation of man pages et al.
        "-DINSTALL_BIN_DIR=${CMAKE_CURRENT_BINARY_DIR}\\fallout"   # I found no better way of preventing zlib from creating & installing DLLs :-/
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /m /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)

add_debug_dep(dep_zlib)

# The following steps are unfortunately needed to remove the _static suffix on libraries
ExternalProject_Add_Step(dep_zlib fix_static
    DEPENDEES install
    COMMAND "${CMAKE_COMMAND}" -E rename zlibstatic.lib zlib.lib
    WORKING_DIRECTORY "${DESTDIR}\\usr\\local\\lib\\"
)
if (${DEP_DEBUG})
    ExternalProject_Add_Step(dep_zlib fix_static_debug
        DEPENDEES install
        COMMAND "${CMAKE_COMMAND}" -E rename zlibstaticd.lib zlibd.lib
        WORKING_DIRECTORY "${DESTDIR}\\usr\\local\\lib\\"
    )
endif ()

if (${DEPS_BITS} EQUAL 32)
    set(DEP_LIBCURL_TARGET "x86")
else ()
    set(DEP_LIBCURL_TARGET "x64")
endif ()

ExternalProject_Add(dep_libcurl
    EXCLUDE_FROM_ALL 1
    URL "https://curl.haxx.se/download/curl-7.58.0.tar.gz"
    URL_HASH SHA256=cc245bf9a1a42a45df491501d97d5593392a03f7b4f07b952793518d97666115
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND cd winbuild && nmake /f Makefile.vc mode=static "VC=${DEP_VS_VER}" GEN_PDB=yes DEBUG=no "MACHINE=${DEP_LIBCURL_TARGET}"
    INSTALL_COMMAND cd builds\\libcurl-*-release-*-winssl
        && "${CMAKE_COMMAND}" -E copy_directory include "${DESTDIR}\\usr\\local\\include"
        && "${CMAKE_COMMAND}" -E copy_directory lib "${DESTDIR}\\usr\\local\\lib"
)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(dep_libcurl SOURCE_DIR)
    ExternalProject_Add_Step(dep_libcurl build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND cd winbuild && nmake /f Makefile.vc mode=static "VC=${DEP_VS_VER}" GEN_PDB=yes DEBUG=yes "MACHINE=${DEP_LIBCURL_TARGET}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
    )
    ExternalProject_Add_Step(dep_libcurl install_debug
        DEPENDEES install
        COMMAND cd builds\\libcurl-*-debug-*-winssl
            && "${CMAKE_COMMAND}" -E copy_directory include "${DESTDIR}\\usr\\local\\include"
            && "${CMAKE_COMMAND}" -E copy_directory lib "${DESTDIR}\\usr\\local\\lib"
        WORKING_DIRECTORY "${SOURCE_DIR}"
    )
endif ()

find_package(Git REQUIRED)

ExternalProject_Add(dep_qhull
    EXCLUDE_FROM_ALL 1
    #URL "https://github.com/qhull/qhull/archive/v7.3.2.tar.gz"
    #URL_HASH SHA256=619c8a954880d545194bc03359404ef36a1abd2dde03678089459757fd790cb0
    GIT_REPOSITORY  https://github.com/qhull/qhull.git
    GIT_TAG         7afedcc73666e46a9f1d74632412ebecf53b1b30 # v7.3.2 plus the mac build patch
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=d
    BUILD_COMMAND msbuild /m /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)

add_debug_dep(dep_qhull)

if (${DEPS_BITS} EQUAL 32)
    set(DEP_WXWIDGETS_TARGET "")
    set(DEP_WXWIDGETS_LIBDIR "vc_lib")
else ()
    set(DEP_WXWIDGETS_TARGET "TARGET_CPU=X64")
    set(DEP_WXWIDGETS_LIBDIR "vc_x64_lib")
endif ()

find_package(Git REQUIRED)

ExternalProject_Add(dep_wxwidgets
    EXCLUDE_FROM_ALL 1
    GIT_REPOSITORY "https://github.com/prusa3d/wxWidgets"
    GIT_TAG v3.1.1-patched
#    URL "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.1/wxWidgets-3.1.1.tar.bz2"
#    URL_HASH SHA256=c925dfe17e8f8b09eb7ea9bfdcfcc13696a3e14e92750effd839f5e10726159e
    BUILD_IN_SOURCE 1
#    PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}\\wxwidgets-pngprefix.h" src\\png\\pngprefix.h
    CONFIGURE_COMMAND ""
    BUILD_COMMAND cd build\\msw && nmake /f makefile.vc BUILD=release SHARED=0 UNICODE=1 USE_GUI=1 "${DEP_WXWIDGETS_TARGET}"
    INSTALL_COMMAND "${CMAKE_COMMAND}" -E copy_directory include "${DESTDIR}\\usr\\local\\include"
        && "${CMAKE_COMMAND}" -E copy_directory "lib\\${DEP_WXWIDGETS_LIBDIR}" "${DESTDIR}\\usr\\local\\lib\\${DEP_WXWIDGETS_LIBDIR}"
)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(dep_wxwidgets SOURCE_DIR)
    ExternalProject_Add_Step(dep_wxwidgets build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND cd build\\msw && nmake /f makefile.vc BUILD=debug SHARED=0 UNICODE=1 USE_GUI=1 "${DEP_WXWIDGETS_TARGET}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
    )
endif ()

ExternalProject_Add(dep_blosc
    EXCLUDE_FROM_ALL 1
    #URL https://github.com/Blosc/c-blosc/archive/v1.17.0.zip
    #URL_HASH SHA256=7463a1df566704f212263312717ab2c36b45d45cba6cd0dccebf91b2cc4b4da9
    GIT_REPOSITORY https://github.com/Blosc/c-blosc.git
    GIT_TAG e63775855294b50820ef44d1b157f4de1cc38d3e #v1.17.0
    DEPENDS dep_zlib
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=d
        -DBUILD_SHARED=OFF 
        -DBUILD_STATIC=ON
        -DBUILD_TESTS=OFF 
        -DBUILD_BENCHMARKS=OFF 
        -DPREFER_EXTERNAL_ZLIB=ON
        -DBLOSC_IS_SUBPROJECT:BOOL=ON
        -DBLOSC_INSTALL:BOOL=ON
    PATCH_COMMAND       ${GIT_EXECUTABLE} checkout -f -- . && git clean -df && 
                        ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_SOURCE_DIR}/blosc-mods.patch
    BUILD_COMMAND msbuild /m /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)

add_debug_dep(dep_blosc)

ExternalProject_Add(dep_openexr
    EXCLUDE_FROM_ALL 1
    GIT_REPOSITORY https://github.com/openexr/openexr.git
    GIT_TAG eae0e337c9f5117e78114fd05f7a415819df413a #v2.4.0 
    DEPENDS dep_zlib
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_TESTING=OFF 
        -DPYILMBASE_ENABLE:BOOL=OFF 
        -DOPENEXR_VIEWERS_ENABLE:BOOL=OFF
        -DOPENEXR_BUILD_UTILS:BOOL=OFF
    BUILD_COMMAND msbuild /m /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)

add_debug_dep(dep_openexr)

ExternalProject_Add(dep_openvdb
    EXCLUDE_FROM_ALL 1
    #URL https://github.com/AcademySoftwareFoundation/openvdb/archive/v6.2.1.zip
    #URL_HASH SHA256=dc337399dce8e1c9f21f20e97b1ce7e4933cb0a63bb3b8b734d8fcc464aa0c48
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openvdb.git
    GIT_TAG  aebaf8d95be5e57fd33949281ec357db4a576c2e #v6.2.1
    DEPENDS dep_blosc dep_openexr dep_tbb dep_boost
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        -DCMAKE_DEBUG_POSTFIX=d
        -DCMAKE_PREFIX_PATH=${DESTDIR}/usr/local
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DOPENVDB_BUILD_PYTHON_MODULE=OFF 
        -DUSE_BLOSC=ON 
        -DOPENVDB_CORE_SHARED=OFF 
        -DOPENVDB_CORE_STATIC=ON 
        -DTBB_STATIC=ON
        -DOPENVDB_BUILD_VDB_PRINT=ON
    BUILD_COMMAND msbuild /m /P:Configuration=Release INSTALL.vcxproj
    PATCH_COMMAND       ${GIT_EXECUTABLE} checkout -f -- . && git clean -df && 
                        ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_SOURCE_DIR}/openvdb-mods.patch
    INSTALL_COMMAND ""
)

if (${DEP_DEBUG})
    ExternalProject_Get_Property(dep_openvdb BINARY_DIR)
    ExternalProject_Add_Step(dep_openvdb build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND ${CMAKE_COMMAND} ../dep_openvdb -DOPENVDB_BUILD_VDB_PRINT=OFF
        COMMAND msbuild /m /P:Configuration=Debug INSTALL.vcxproj
        WORKING_DIRECTORY "${BINARY_DIR}"
    )
endif ()
