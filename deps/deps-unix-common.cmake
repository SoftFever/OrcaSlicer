
# The unix common part expects DEP_CMAKE_OPTS to be set

if (MINGW)
    set(TBB_MINGW_WORKAROUND "-flifetime-dse=1")
else ()
    set(TBB_MINGW_WORKAROUND "")
endif ()

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
    URL "https://github.com/qhull/qhull/archive/v7.3.2.tar.gz"
    URL_HASH SHA256=619c8a954880d545194bc03359404ef36a1abd2dde03678089459757fd790cb0
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        ${DEP_CMAKE_OPTS}
    PATCH_COMMAND ${GIT_EXECUTABLE} apply --ignore-space-change --ignore-whitespace ${CMAKE_CURRENT_SOURCE_DIR}/qhull-mods.patch
)

ExternalProject_Add(dep_libigl
    EXCLUDE_FROM_ALL 1
    URL "https://github.com/libigl/libigl/archive/v2.0.0.tar.gz"
    URL_HASH SHA256=42518e6b106c7209c73435fd260ed5d34edeb254852495b4c95dce2d95401328
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
        -DLIBIGL_BUILD_PYTHON=OFF
        -DLIBIGL_BUILD_TESTS=OFF
        -DLIBIGL_BUILD_TUTORIALS=OFF
        -DLIBIGL_USE_STATIC_LIBRARY=OFF #${DEP_BUILD_IGL_STATIC}
        -DLIBIGL_WITHOUT_COPYLEFT=OFF
        -DLIBIGL_WITH_CGAL=OFF
        -DLIBIGL_WITH_COMISO=OFF
        -DLIBIGL_WITH_CORK=OFF
        -DLIBIGL_WITH_EMBREE=OFF
        -DLIBIGL_WITH_MATLAB=OFF
        -DLIBIGL_WITH_MOSEK=OFF
        -DLIBIGL_WITH_OPENGL=OFF
        -DLIBIGL_WITH_OPENGL_GLFW=OFF
        -DLIBIGL_WITH_OPENGL_GLFW_IMGUI=OFF
        -DLIBIGL_WITH_PNG=OFF
        -DLIBIGL_WITH_PYTHON=OFF
        -DLIBIGL_WITH_TETGEN=OFF
        -DLIBIGL_WITH_TRIANGLE=OFF
        -DLIBIGL_WITH_XML=OFF
    PATCH_COMMAND ${GIT_EXECUTABLE} apply --ignore-space-change --ignore-whitespace ${CMAKE_CURRENT_SOURCE_DIR}/igl-mods.patch
)

