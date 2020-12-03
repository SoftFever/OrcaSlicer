if (APPLE)
    # Only disable NEON extension for Apple ARM builds, leave it enabled for Raspberry PI.
    set(_disable_neon_extension "-DPNG_ARM_NEON=off")
else ()
    set(_disable_neon_extension "")
endif ()

prusaslicer_add_cmake_project(PNG 
    GIT_REPOSITORY https://github.com/glennrp/libpng.git 
    GIT_TAG v1.6.35
    DEPENDS ${ZLIB_PKG}
    CMAKE_ARGS
        -DPNG_SHARED=OFF
        -DPNG_STATIC=ON
        -DPNG_PREFIX=prusaslicer_
        -DPNG_TESTS=OFF
        -DDISABLE_DEPENDENCY_TRACKING=OFF
        ${_disable_neon_extension}
)

if (MSVC)
    add_debug_dep(dep_PNG)
endif ()
