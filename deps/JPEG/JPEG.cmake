prusaslicer_add_cmake_project(JPEG
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/2.0.6.zip
    URL_HASH SHA256=017bdc33ff3a72e11301c0feb4657cb27719d7f97fa67a78ed506c594218bbf1
    DEPENDS ${ZLIB_PKG}
    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
)
