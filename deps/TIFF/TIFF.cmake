find_package(OpenGL QUIET REQUIRED)

if (APPLE)
    message(STATUS "Compiling TIFF for macos ${CMAKE_SYSTEM_VERSION}.")
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
else()
    orcaslicer_add_cmake_project(TIFF
        URL https://gitlab.com/libtiff/libtiff/-/archive/v4.1.0/libtiff-v4.1.0.zip
        URL_HASH SHA256=17a3e875acece9be40b093361cfef47385d4ef22c995ffbf36b2871f5785f9b8
        DEPENDS ${ZLIB_PKG} ${PNG_PKG} dep_JPEG
        CMAKE_ARGS
            -Dlzma:BOOL=OFF
            -Dwebp:BOOL=OFF
            -Djbig:BOOL=OFF
            -Dzstd:BOOL=OFF
            -Dpixarlog:BOOL=OFF
    )

endif()