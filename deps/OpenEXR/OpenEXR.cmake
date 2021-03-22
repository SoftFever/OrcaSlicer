prusaslicer_add_cmake_project(OpenEXR
    GIT_REPOSITORY https://github.com/openexr/openexr.git
    DEPENDS ${ZLIB_PKG}
    GIT_TAG eae0e337c9f5117e78114fd05f7a415819df413a #v2.4.0
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