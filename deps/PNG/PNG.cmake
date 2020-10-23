set(_prefix_line "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_prefix_line "-DPNG_PREFIX=prusaslicer_")
endif()

prusaslicer_add_cmake_project(PNG 
    GIT_REPOSITORY https://github.com/glennrp/libpng.git 
    GIT_TAG v1.6.35
    DEPENDS ${ZLIB_PKG}
    CMAKE_ARGS
        -DPNG_SHARED=OFF
        -DPNG_STATIC=ON
        ${_prefix_line}
        -DPNG_TESTS=OFF
)

if (MSVC)
    add_debug_dep(dep_PNG)
endif ()
