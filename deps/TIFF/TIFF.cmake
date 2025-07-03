find_package(OpenGL QUIET REQUIRED)

if (APPLE)
    message(STATUS "Compiling TIFF for macos ${CMAKE_SYSTEM_VERSION}.")
    orcaslicer_add_cmake_project(TIFF
        URL https://gitlab.com/libtiff/libtiff/-/archive/v4.3.0/libtiff-v4.3.0.zip
        URL_HASH SHA256=455abecf8fba9754b80f8eff01c3ef5b24a3872ffce58337a59cba38029f0eca
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
        URL_HASH SHA256=c56edfacef0a60c0de3e6489194fcb2f24c03dbb550a8a7de5938642d045bd32
        DEPENDS ${ZLIB_PKG} ${PNG_PKG} dep_JPEG
        CMAKE_ARGS
            -Dlzma:BOOL=OFF
            -Dwebp:BOOL=OFF
            -Djbig:BOOL=OFF
            -Dzstd:BOOL=OFF
            -Dpixarlog:BOOL=OFF
    )

endif()



