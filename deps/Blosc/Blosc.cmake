if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

prusaslicer_add_cmake_project(Blosc
    GIT_REPOSITORY https://github.com/Blosc/c-blosc.git
    GIT_TAG e63775855294b50820ef44d1b157f4de1cc38d3e #v1.17.0
    DEPENDS ${ZLIB_PKG}
    PATCH_COMMAND       ${GIT_EXECUTABLE} reset --hard && ${GIT_EXECUTABLE} clean -df &&
                        ${GIT_EXECUTABLE} apply --whitespace=nowarn ${CMAKE_CURRENT_LIST_DIR}/blosc-mods.patch
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED=${_build_shared} 
        -DBUILD_STATIC=${_build_static}
        -DBUILD_TESTS=OFF 
        -DBUILD_BENCHMARKS=OFF 
        -DPREFER_EXTERNAL_ZLIB=ON
)

if (MSVC)
    add_debug_dep(dep_Blosc)
endif ()