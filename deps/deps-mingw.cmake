set(DEP_CMAKE_OPTS "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
set(DEP_BOOST_TOOLSET "gcc")
set(DEP_BITS 64)

find_package(Git REQUIRED)

# TODO make sure to build tbb with -flifetime-dse=1
include("deps-unix-common.cmake")

ExternalProject_Add(dep_boost
    EXCLUDE_FROM_ALL 1
    URL "https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz"
    URL_HASH SHA256=882b48708d211a5f48e60b0124cf5863c1534cd544ecd0664bb534a4b5d506e9
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
        "toolset=${DEP_BOOST_TOOLSET}"
        link=static
        define=BOOST_USE_WINAPI_VERSION=0x0502
        variant=release
        threading=multi
        boost.locale.icu=off
        "${DEP_BOOST_DEBUG}" release install
    INSTALL_COMMAND ""   # b2 does that already
)

ExternalProject_Add(dep_libcurl
    EXCLUDE_FROM_ALL 1
    URL "https://curl.haxx.se/download/curl-7.58.0.tar.gz"
    URL_HASH SHA256=cc245bf9a1a42a45df491501d97d5593392a03f7b4f07b952793518d97666115
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTING=OFF
        -DCURL_STATICLIB=ON
        -DCURL_STATIC_CRT=ON
        -DENABLE_THREADED_RESOLVER=ON
        -DCURL_DISABLE_FTP=ON
        -DCURL_DISABLE_LDAP=ON
        -DCURL_DISABLE_LDAPS=ON
        -DCURL_DISABLE_TELNET=ON
        -DCURL_DISABLE_DICT=ON
        -DCURL_DISABLE_FILE=ON
        -DCURL_DISABLE_TFTP=ON
        -DCURL_DISABLE_RTSP=ON
        -DCURL_DISABLE_POP3=ON
        -DCURL_DISABLE_IMAP=ON
        -DCURL_DISABLE_SMTP=ON
        -DCURL_DISABLE_GOPHER=ON
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        ${DEP_CMAKE_OPTS}
)

ExternalProject_Add(dep_wxwidgets
    EXCLUDE_FROM_ALL 1
    GIT_REPOSITORY "https://github.com/prusa3d/wxWidgets"
    GIT_TAG v3.1.1-patched
#    URL "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.1/wxWidgets-3.1.1.tar.bz2"
#    URL_HASH SHA256=c925dfe17e8f8b09eb7ea9bfdcfcc13696a3e14e92750effd839f5e10726159e
#    PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}\\wxwidgets-pngprefix.h" src\\png\\pngprefix.h
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DwxUSE_LIBPNG=builtin
        -DwxUSE_ZLIB=builtin
        -DwxUSE_OPENGL=ON
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        ${DEP_CMAKE_OPTS}
)