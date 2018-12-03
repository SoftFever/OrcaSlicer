
set(DEP_CMAKE_OPTS
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    "-DCMAKE_OSX_SYSROOT=${DEPS_OSX_SYSROOT}"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DEPS_OSX_TARGET}"
)

include("deps-unix-common.cmake")


ExternalProject_Add(dep_boost
    EXCLUDE_FROM_ALL 1
    URL "https://dl.bintray.com/boostorg/release/1.66.0/source/boost_1_66_0.tar.gz"
    URL_HASH SHA256=bd0df411efd9a585e5a2212275f8762079fed8842264954675a4fddc46cfcf60
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./bootstrap.sh
        --with-libraries=system,filesystem,thread,log,locale,regex
        "--prefix=${DESTDIR}/usr/local"
    BUILD_COMMAND ./b2
        -j ${NPROC}
        --reconfigure
        link=static
        variant=release
        threading=multi
        boost.locale.icu=off
        "cflags=cflags=-fPIC -mmacosx-version-min=${DEPS_OSX_TARGET}"
        "cxxflags=cxxflags=-fPIC -mmacosx-version-min=${DEPS_OSX_TARGET}"
        install
    INSTALL_COMMAND ""   # b2 does that already
)

ExternalProject_Add(dep_libcurl
    EXCLUDE_FROM_ALL 1
    URL "https://curl.haxx.se/download/curl-7.58.0.tar.gz"
    URL_HASH SHA256=cc245bf9a1a42a45df491501d97d5593392a03f7b4f07b952793518d97666115
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./configure
        --enable-static
        --disable-shared
        "--with-ssl=${DESTDIR}/usr/local"
        --with-pic
        --enable-ipv6
        --enable-versioned-symbols
        --enable-threaded-resolver
        --with-darwinssl
        --without-ssl     # disables OpenSSL
        --disable-ldap
        --disable-ldaps
        --disable-manual
        --disable-rtsp
        --disable-dict
        --disable-telnet
        --disable-pop3
        --disable-imap
        --disable-smb
        --disable-smtp
        --disable-gopher
        --disable-crypto-auth
        --without-gssapi
        --without-libpsl
        --without-libidn2
        --without-gnutls
        --without-polarssl
        --without-mbedtls
        --without-cyassl
        --without-nss
        --without-axtls
        --without-brotli
        --without-libmetalink
        --without-libssh
        --without-libssh2
        --without-librtmp
        --without-nghttp2
        --without-zsh-functions-dir
    BUILD_COMMAND make "-j${NPROC}"
    INSTALL_COMMAND make install "DESTDIR=${DESTDIR}"
)

ExternalProject_Add(dep_wxwidgets
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.1/wxWidgets-3.1.1.tar.bz2"
    URL_HASH SHA256=c925dfe17e8f8b09eb7ea9bfdcfcc13696a3e14e92750effd839f5e10726159e
    BUILD_IN_SOURCE 1
    PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/wxwidgets-pngprefix.h" src/png/pngprefix.h
    CONFIGURE_COMMAND ./configure
        "--prefix=${DESTDIR}/usr/local"
        --disable-shared
        --with-osx_cocoa
    	"--with-macosx-version-min=${DEPS_OSX_TARGET}"
        "--with-macosx-sdk=${DEPS_OSX_SYSROOT}"
        --with-opengl
        --with-regex=builtin
        --with-libpng=builtin
        --with-libxpm=builtin
        --with-libjpeg=builtin
        --with-libtiff=builtin
        --with-zlib=builtin
        --with-expat=builtin
        --disable-debug
        --disable-debug_flag
    BUILD_COMMAND make "-j${NPROC}" && make -C locale allmo
    INSTALL_COMMAND make install
)
