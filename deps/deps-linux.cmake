
set(DEP_CMAKE_OPTS "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")

include("deps-unix-common.cmake")

ExternalProject_Add(dep_boost
    EXCLUDE_FROM_ALL 1
    URL "https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz"
    URL_HASH SHA256=882b48708d211a5f48e60b0124cf5863c1534cd544ecd0664bb534a4b5d506e9
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./bootstrap.sh
        --with-libraries=system,iostreams,filesystem,thread,log,locale,regex
        "--prefix=${DESTDIR}/usr/local"
    BUILD_COMMAND ./b2
        -j ${NPROC}
        --reconfigure
        link=static
        variant=release
        threading=multi
        boost.locale.icu=off
        cflags=-fPIC
        cxxflags=-fPIC
        install
    INSTALL_COMMAND ""   # b2 does that already
)

ExternalProject_Add(dep_libopenssl
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/openssl/openssl/archive/OpenSSL_1_1_0l.tar.gz"
    URL_HASH SHA256=e2acf0cf58d9bff2b42f2dc0aee79340c8ffe2c5e45d3ca4533dd5d4f5775b1d
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./config
        "--prefix=${DESTDIR}/usr/local"
        no-shared
        no-ssl3-method
        no-dynamic-engine
        -Wa,--noexecstack
    BUILD_COMMAND make depend && make "-j${NPROC}"
    INSTALL_COMMAND make install_sw
)

ExternalProject_Add(dep_libcurl
    EXCLUDE_FROM_ALL 1
    DEPENDS dep_libopenssl
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
        --with-random=/dev/urandom
        --with-ca-bundle=/etc/ssl/certs/ca-certificates.crt
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

if (DEP_WX_STABLE)
    set(DEP_WX_URL "https://github.com/wxWidgets/wxWidgets/releases/download/v3.0.4/wxWidgets-3.0.4.tar.bz2")
    set(DEP_WX_HASH "SHA256=96157f988d261b7368e5340afa1a0cad943768f35929c22841f62c25b17bf7f0")
else ()
    set(DEP_WX_URL "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.1/wxWidgets-3.1.1.tar.bz2")
    set(DEP_WX_HASH "SHA256=c925dfe17e8f8b09eb7ea9bfdcfcc13696a3e14e92750effd839f5e10726159e")
endif()

ExternalProject_Add(dep_wxwidgets
    EXCLUDE_FROM_ALL 1
    URL "${DEP_WX_URL}"
    URL_HASH "${DEP_WX_HASH}"
    BUILD_IN_SOURCE 1
    PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/wxwidgets-pngprefix.h" src/png/pngprefix.h
    CONFIGURE_COMMAND ./configure
        "--prefix=${DESTDIR}/usr/local"
        --disable-shared
        --with-gtk=2
        --with-opengl
        --enable-unicode
        --enable-graphics_ctx
        --with-regex=builtin
        --with-libpng=builtin
        --with-libxpm=builtin
        --with-libjpeg=builtin
        --with-libtiff=builtin
        --with-zlib
        --with-expat=builtin
        --disable-precomp-headers
        --enable-debug_info
        --enable-debug_gdb
        --disable-debug
        --disable-debug_flag
    BUILD_COMMAND make "-j${NPROC}" && make -C locale allmo
    INSTALL_COMMAND make install
)

add_dependencies(dep_openvdb dep_boost)