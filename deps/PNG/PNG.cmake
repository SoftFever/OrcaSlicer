if (APPLE)
    # Only disable NEON extension for Apple ARM builds, leave it enabled for Raspberry PI.
    set(_disable_neon_extension "-DPNG_ARM_NEON=off")
else ()
    set(_disable_neon_extension "")
endif ()

set(_patch_step "")
if (APPLE)
    set(_patch_step PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/PNG.patch)
endif ()

prusaslicer_add_cmake_project(PNG 
    # GIT_REPOSITORY https://github.com/glennrp/libpng.git 
    # GIT_TAG v1.6.35
    URL https://github.com/glennrp/libpng/archive/refs/tags/v1.6.35.zip
    URL_HASH SHA256=3d22d46c566b1761a0e15ea397589b3a5f36ac09b7c785382e6470156c04247f
    DEPENDS ${ZLIB_PKG}
    "${_patch_step}"
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
