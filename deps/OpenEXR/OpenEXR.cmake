prusaslicer_add_cmake_project(OpenEXR
    GIT_REPOSITORY https://github.com/openexr/openexr.git
    DEPENDS ${ZLIB_PKG}
    GIT_TAG v2.5.5
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_TESTING=OFF 
        -DPYILMBASE_ENABLE:BOOL=OFF 
        -DOPENEXR_VIEWERS_ENABLE:BOOL=OFF
        -DOPENEXR_BUILD_UTILS:BOOL=OFF
)

if (MSVC)
    add_debug_dep(dep_OpenEXR)
endif ()