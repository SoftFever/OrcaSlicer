find_package(OpenGL QUIET REQUIRED)

orcaslicer_add_cmake_project(TIFF
    URL https://gitlab.com/libtiff/libtiff/-/archive/v4.3.0/libtiff-v4.3.0.zip
    URL_HASH SHA256=4fca1b582c88319f3ad6ecd5b46320eadaf5eb4ef6f6c32d44caaae4a03d0726
    DEPENDS ${ZLIB_PKG} ${PNG_PKG} dep_JPEG
    CMAKE_ARGS
        -Dlzma:BOOL=OFF
        -Dwebp:BOOL=OFF
        -Djbig:BOOL=OFF
        -Dzstd:BOOL=OFF
        -Dlibdeflate:BOOL=OFF
        -Dpixarlog:BOOL=OFF
)