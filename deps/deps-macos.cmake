
# This ensures dependencies don't use SDK features which are not available in the version specified by Deployment target
# That can happen when one uses a recent SDK but specifies an older Deployment target
set(DEP_WERRORS_SDK "-Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")

set(DEP_CMAKE_OPTS
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DEP_OSX_TARGET}"
    "-DCMAKE_CXX_FLAGS=${DEP_WERRORS_SDK}"
    "-DCMAKE_C_FLAGS=${DEP_WERRORS_SDK}"
)

include("deps-unix-common.cmake")


ExternalProject_Add(dep_boost
    EXCLUDE_FROM_ALL 1
    URL "https://dl.bintray.com/boostorg/release/1.71.0/source/boost_1_71_0.tar.gz"
    URL_HASH SHA256=96b34f7468f26a141f6020efb813f1a2f3dfb9797ecf76a7d7cbd843cc95f5bd
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./bootstrap.sh
        --with-toolset=clang
        --with-libraries=system,iostreams,filesystem,thread,log,locale,regex
        "--prefix=${DESTDIR}/usr/local"
    BUILD_COMMAND ./b2
        -j ${NPROC}
        --reconfigure
        toolset=clang
        link=static
        variant=release
        threading=multi
        boost.locale.icu=off
        "cflags=-fPIC -mmacosx-version-min=${DEP_OSX_TARGET}"
        "cxxflags=-fPIC -mmacosx-version-min=${DEP_OSX_TARGET}"
        "mflags=-fPIC -mmacosx-version-min=${DEP_OSX_TARGET}"
        "mmflags=-fPIC -mmacosx-version-min=${DEP_OSX_TARGET}"
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
    GIT_REPOSITORY "https://github.com/prusa3d/wxWidgets"
    GIT_TAG v3.1.1-patched
#    URL "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.2/wxWidgets-3.1.2.tar.bz2"
#    URL_HASH SHA256=4cb8d23d70f9261debf7d6cfeca667fc0a7d2b6565adb8f1c484f9b674f1f27a
    BUILD_IN_SOURCE 1
#    PATCH_COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/wxwidgets-pngprefix.h" src/png/pngprefix.h
    CONFIGURE_COMMAND env "CXXFLAGS=${DEP_WERRORS_SDK}" "CFLAGS=${DEP_WERRORS_SDK}" ./configure
        "--prefix=${DESTDIR}/usr/local"
        --disable-shared
        --with-osx_cocoa
        --with-macosx-sdk=${CMAKE_OSX_SYSROOT}
        "--with-macosx-version-min=${DEP_OSX_TARGET}"
        --with-opengl
        --with-regex=builtin
        --with-libpng=builtin
        --with-libxpm=builtin
        --with-libjpeg=builtin
        --with-libtiff=builtin
        --with-zlib
        --with-expat=builtin
        --disable-debug
        --disable-debug_flag
    BUILD_COMMAND make "-j${NPROC}" && PATH=/usr/local/opt/gettext/bin/:$ENV{PATH} make -C locale allmo
    INSTALL_COMMAND make install
)

add_dependencies(dep_openvdb dep_boost)