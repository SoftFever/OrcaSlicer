orcaslicer_add_cmake_project(PNG 
	GIT_REPOSITORY https://github.com/glennrp/libpng.git 
        GIT_TAG v1.6.59
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
