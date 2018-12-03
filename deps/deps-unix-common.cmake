
# The unix common part expects DEP_CMAKE_OPTS to be set

ExternalProject_Add(dep_tbb
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/wjakob/tbb/archive/a0dc9bf76d0120f917b641ed095360448cabc85b.tar.gz"
    URL_HASH SHA256=0545cb6033bd1873fcae3ea304def720a380a88292726943ae3b9b207f322efe
    CMAKE_ARGS
        -DTBB_BUILD_SHARED=OFF
        -DTBB_BUILD_TESTS=OFF
        ${DEP_CMAKE_OPTS}
    INSTALL_COMMAND make install "DESTDIR=${DESTDIR}"
)

ExternalProject_Add(dep_gtest
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/google/googletest/archive/release-1.8.1.tar.gz"
    URL_HASH SHA256=9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c
    CMAKE_ARGS -DBUILD_GMOCK=OFF ${DEP_CMAKE_OPTS}
    INSTALL_COMMAND make install "DESTDIR=${DESTDIR}"
)

ExternalProject_Add(dep_nlopt
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/stevengj/nlopt/archive/v2.5.0.tar.gz"
    URL_HASH SHA256=c6dd7a5701fff8ad5ebb45a3dc8e757e61d52658de3918e38bab233e7fd3b4ae
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DNLOPT_PYTHON=OFF
        -DNLOPT_OCTAVE=OFF
        -DNLOPT_MATLAB=OFF
        -DNLOPT_GUILE=OFF
        ${DEP_CMAKE_OPTS}
    INSTALL_COMMAND make install "DESTDIR=${DESTDIR}"
    INSTALL_COMMAND ""
)

ExternalProject_Add(dep_libpng
    EXCLUDE_FROM_ALL 1
    URL "http://prdownloads.sourceforge.net/libpng/libpng-1.6.35.tar.xz?download"
    URL_HASH SHA256=23912ec8c9584917ed9b09c5023465d71709dce089be503c7867fec68a93bcd7
    CMAKE_GENERATOR "${DEP_MSVC_GEN}"
    CMAKE_ARGS
        -DPNG_SHARED=OFF
        -DPNG_TESTS=OFF
        ${DEP_CMAKE_OPTS}
    INSTALL_COMMAND make install "DESTDIR=${DESTDIR}"
    INSTALL_COMMAND ""
)
