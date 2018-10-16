
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
        install
    INSTALL_COMMAND ""   # b2 does that already
)


if (${DEPS_BITS} EQUAL 32)
    set(DEP_TBB_GEN "Visual Studio 12")
else ()
    set(DEP_TBB_GEN "Visual Studio 12 Win64")
endif ()

ExternalProject_Add(dep_tbb
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/wjakob/tbb/archive/a0dc9bf76d0120f917b641ed095360448cabc85b.tar.gz"
    URL_HASH SHA256=0545cb6033bd1873fcae3ea304def720a380a88292726943ae3b9b207f322efe
    CMAKE_GENERATOR "${DEP_TBB_GEN}"
    CMAKE_ARGS
        -DCMAKE_CONFIGURATION_TYPES=Release
        -DTBB_BUILD_SHARED=OFF
        -DTBB_BUILD_TESTS=OFF
        "-DCMAKE_INSTALL_PREFIX:PATH=${DESTDIR}\\usr\\local"
    BUILD_COMMAND msbuild /P:Configuration=Release INSTALL.vcxproj
    INSTALL_COMMAND ""
)


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
    INSTALL_COMMAND cd builds\\libcurl-*-winssl
        && "${CMAKE_COMMAND}" -E copy_directory include "${DESTDIR}\\usr\\local\\include"
        && "${CMAKE_COMMAND}" -E copy_directory lib "${DESTDIR}\\usr\\local\\lib"
)

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
    BUILD_COMMAND cd build\\msw && nmake /f makefile.vc
        BUILD=release
        SHARED=0
        UNICODE=1
        USE_GUI=1
        "${DEP_WXWIDGETS_TARGET}"
    INSTALL_COMMAND "${CMAKE_COMMAND}" -E copy_directory include "${DESTDIR}\\usr\\local\\include"
        && "${CMAKE_COMMAND}" -E copy_directory "lib\\${DEP_WXWIDGETS_LIBDIR}" "${DESTDIR}\\usr\\local\\lib\\${DEP_WXWIDGETS_LIBDIR}"
)
