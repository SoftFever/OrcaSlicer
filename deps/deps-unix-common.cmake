
# The unix common part expects DEP_CMAKE_OPTS to be set

if (MINGW)
    set(TBB_MINGW_WORKAROUND "-flifetime-dse=1")
else ()
    set(TBB_MINGW_WORKAROUND "")
endif ()

find_package(ZLIB REQUIRED)

ExternalProject_Add(dep_tbb
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/wjakob/tbb/archive/a0dc9bf76d0120f917b641ed095360448cabc85b.tar.gz"
    URL_HASH SHA256=0545cb6033bd1873fcae3ea304def720a380a88292726943ae3b9b207f322efe
    CMAKE_ARGS
        -DTBB_BUILD_SHARED=OFF
        -DTBB_BUILD_TESTS=OFF
        -DCMAKE_CXX_FLAGS=${TBB_MINGW_WORKAROUND}
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        ${DEP_CMAKE_OPTS}
)

ExternalProject_Add(dep_gtest
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/google/googletest/archive/release-1.8.1.tar.gz"
    URL_HASH SHA256=9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c
    CMAKE_ARGS -DBUILD_GMOCK=OFF ${DEP_CMAKE_OPTS} -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
)

ExternalProject_Add(dep_cereal
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/USCiLab/cereal/archive/v1.2.2.tar.gz"
#    URL_HASH SHA256=c6dd7a5701fff8ad5ebb45a3dc8e757e61d52658de3918e38bab233e7fd3b4ae
    CMAKE_ARGS
        -DJUST_INSTALL_CEREAL=on
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        ${DEP_CMAKE_OPTS}
)

ExternalProject_Add(dep_nlopt
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/stevengj/nlopt/archive/v2.5.0.tar.gz"
    URL_HASH SHA256=c6dd7a5701fff8ad5ebb45a3dc8e757e61d52658de3918e38bab233e7fd3b4ae
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DNLOPT_PYTHON=OFF
        -DNLOPT_OCTAVE=OFF
        -DNLOPT_MATLAB=OFF
        -DNLOPT_GUILE=OFF
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        ${DEP_CMAKE_OPTS}
)
find_package(Git REQUIRED)

ExternalProject_Add(dep_qhull
    EXCLUDE_FROM_ALL 1
    #URL "https://github.com/qhull/qhull/archive/v7.3.2.tar.gz"
    #URL_HASH SHA256=619c8a954880d545194bc03359404ef36a1abd2dde03678089459757fd790cb0
    GIT_REPOSITORY  https://github.com/qhull/qhull.git
    GIT_TAG         7afedcc73666e46a9f1d74632412ebecf53b1b30 # v7.3.2 plus the mac build patch
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        ${DEP_CMAKE_OPTS}
)

ExternalProject_Add(dep_blosc
    EXCLUDE_FROM_ALL 1
    GIT_REPOSITORY https://github.com/Blosc/c-blosc.git
    GIT_TAG e63775855294b50820ef44d1b157f4de1cc38d3e #v1.17.0
    DEPENDS
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
    PATCH_COMMAND       ${GIT_EXECUTABLE} reset --hard && git clean -df && 
                        ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_SOURCE_DIR}/blosc-mods.patch
)

ExternalProject_Add(dep_openexr
    EXCLUDE_FROM_ALL 1
    GIT_REPOSITORY https://github.com/openexr/openexr.git
    GIT_TAG eae0e337c9f5117e78114fd05f7a415819df413a #v2.4.0 
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_TESTING=OFF 
        -DPYILMBASE_ENABLE:BOOL=OFF 
        -DOPENEXR_VIEWERS_ENABLE:BOOL=OFF
        -DOPENEXR_BUILD_UTILS:BOOL=OFF
)

ExternalProject_Add(dep_openvdb
    EXCLUDE_FROM_ALL 1
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openvdb.git
    GIT_TAG aebaf8d95be5e57fd33949281ec357db4a576c2e #v6.2.1
    DEPENDS dep_blosc dep_openexr dep_tbb
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
    PATCH_COMMAND PATCH_COMMAND     ${GIT_EXECUTABLE} checkout -f -- . && git clean -df && 
                                    ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_SOURCE_DIR}/openvdb-mods.patch
)