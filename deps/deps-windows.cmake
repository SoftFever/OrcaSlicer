
if (${DEPS_BITS} EQUAL 32)
    set(DEP_MSVC_GEN "Visual Studio 12")
else ()
    set(DEP_MSVC_GEN "Visual Studio 12 Win64")
endif ()



if (${DEP_DEBUG})
    set(DEP_BOOST_DEBUG "debug")
else ()
    set(DEP_BOOST_DEBUG "")
endif ()

ExternalProject_Add(dep_boost
    EXCLUDE_FROM_ALL 1
    URL "https://dl.bintray.com/boostorg/release/1.63.0/source/boost_1_63_0.tar.gz"
    URL_HASH SHA256=fe34a4e119798e10b8cc9e565b3b0284e9fd3977ec8a1b19586ad1dec397088b
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND bootstrap.bat
    BUILD_COMMAND b2.exe
        -j "${NPROC}"
        --with-system
        --with-filesystem
        --with-thread
        --with-log
        --with-locale
        --with-regex
        "--prefix=${DESTDIR}/usr/local"
        "address-model=${DEPS_BITS}"
        toolset=msvc-12.0
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
    CMAKE_ARGS
        -DCMAKE_DEBUG_POSTFIX=d
        -DTBB_BUILD_SHARED=OFF
        -DTBB_BUILD_TESTS=OFF
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(dep_tbb BINARY_DIR)
    ExternalProject_Add_Step(dep_tbb build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND msbuild /P:Configuration=Debug INSTALL.vcxproj
        WORKING_DIRECTORY "${BINARY_DIR}"
    )
endif ()


ExternalProject_Add(dep_gtest
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/google/googletest/archive/release-1.8.1.tar.gz"
    URL_HASH SHA256=9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_ARGS -DBUILD_GMOCK=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(dep_gtest BINARY_DIR)
    ExternalProject_Add_Step(dep_gtest build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND msbuild /P:Configuration=Debug INSTALL.vcxproj
        WORKING_DIRECTORY "${BINARY_DIR}"
    )
endif ()


ExternalProject_Add(dep_nlopt
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/stevengj/nlopt/archive/v2.5.0.tar.gz"
    URL_HASH SHA256=c6dd7a5701fff8ad5ebb45a3dc8e757e61d52658de3918e38bab233e7fd3b4ae
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_ARGS
        -DNLOPT_PYTHON=OFF
        -DNLOPT_OCTAVE=OFF
        -DNLOPT_MATLAB=OFF
        -DNLOPT_GUILE=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=d
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(dep_nlopt BINARY_DIR)
    ExternalProject_Add_Step(dep_nlopt build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND msbuild /P:Configuration=Debug INSTALL.vcxproj
        WORKING_DIRECTORY "${BINARY_DIR}"
    )
endif ()


ExternalProject_Add(dep_zlib
    EXCLUDE_FROM_ALL 1
    URL "https://zlib.net/zlib-1.2.11.tar.xz"
    URL_HASH SHA256=4ff941449631ace0d4d203e3483be9dbc9da454084111f97ea0a2114e19bf066
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(dep_zlib BINARY_DIR)
    ExternalProject_Add_Step(dep_zlib build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND msbuild /P:Configuration=Debug INSTALL.vcxproj
        WORKING_DIRECTORY "${BINARY_DIR}"
    )
endif ()


ExternalProject_Add(dep_libpng
    DEPENDS dep_zlib
    EXCLUDE_FROM_ALL 1
    URL "http://prdownloads.sourceforge.net/libpng/libpng-1.6.35.tar.xz?download"
    URL_HASH SHA256=23912ec8c9584917ed9b09c5023465d71709dce089be503c7867fec68a93bcd7
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_ARGS
        -DPNG_SHARED=OFF
        -DPNG_TESTS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(dep_libpng BINARY_DIR)
    ExternalProject_Add_Step(dep_libpng build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND msbuild /P:Configuration=Debug INSTALL.vcxproj
        WORKING_DIRECTORY "${BINARY_DIR}"
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
    BUILD_COMMAND cd winbuild && nmake /f Makefile.vc mode=static VC=12 GEN_PDB=yes DEBUG=no "MACHINE=${DEP_LIBCURL_TARGET}"
    INSTALL_COMMAND cd builds\\libcurl-*-release-*-winssl
        && "${CMAKE_COMMAND}" -E copy_directory include "${DESTDIR}\\usr\\local\\include"
        && "${CMAKE_COMMAND}" -E copy_directory lib "${DESTDIR}\\usr\\local\\lib"
)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(dep_libcurl SOURCE_DIR)
    ExternalProject_Add_Step(dep_libcurl build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND cd winbuild && nmake /f Makefile.vc mode=static VC=12 GEN_PDB=yes DEBUG=yes "MACHINE=${DEP_LIBCURL_TARGET}"
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


if (${DEPS_BITS} EQUAL 32)
    set(DEP_WXWIDGETS_TARGET "")
    set(DEP_WXWIDGETS_LIBDIR "vc_lib")
else ()
    set(DEP_WXWIDGETS_TARGET "TARGET_CPU=X64")
    set(DEP_WXWIDGETS_LIBDIR "vc_x64_lib")
endif ()

ExternalProject_Add(dep_wxwidgets
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.1/wxWidgets-3.1.1.tar.bz2"
    URL_HASH SHA256=c925dfe17e8f8b09eb7ea9bfdcfcc13696a3e14e92750effd839f5e10726159e
    BUILD_IN_SOURCE 1
    PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}\\wxwidgets-pngprefix.h" src\\png\\pngprefix.h
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
