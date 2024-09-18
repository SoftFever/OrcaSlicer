find_package(OpenGL QUIET REQUIRED)

if (APPLE)
    set(URL "https://gitlab.com/libtiff/libtiff/-/archive/v4.3.0/libtiff-v4.3.0.zip")
    set(HASH "SHA256=4fca1b582c88319f3ad6ecd5b46320eadaf5eb4ef6f6c32d44caaae4a03d0726")
    set(_extra_args -Dlibdeflate:BOOL=OFF)
else ()
    set(URL "https://gitlab.com/libtiff/libtiff/-/archive/v4.1.0/libtiff-v4.1.0.zip")
    set(HASH "SHA256=c56edfacef0a60c0de3e6489194fcb2f24c03dbb550a8a7de5938642d045bd32")
    unset(_extra_args)
endif ()

orcaslicer_add_cmake_project(TIFF
        URL ${URL}
        URL_HASH ${HASH}
        DEPENDS ${ZLIB_PKG} ${PNG_PKG} dep_JPEG
        CMAKE_ARGS
            -Dlzma:BOOL=OFF
            -Dwebp:BOOL=OFF
            -Djbig:BOOL=OFF
            -Dzstd:BOOL=OFF
            -Dpixarlog:BOOL=OFF
            ${_extra_args}
    )



