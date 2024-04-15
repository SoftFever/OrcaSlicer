include(GNUInstallDirs)

if(LINUX AND CMAKE_BUILD_TYPE STREQUAL "Debug") # compiling in debug mode on linux causes errors
    set(_build_args "-DCMAKE_BUILD_TYPE=RelWithDebInfo")
endif()

orcaslicer_add_cmake_project(Qhull
    URL "https://github.com/qhull/qhull/archive/v8.0.1.zip"
    URL_HASH SHA256=5287f5edd6a0372588f5d6640799086a4033d89d19711023ef8229dd9301d69b
    CMAKE_ARGS 
        -DINCLUDE_INSTALL_DIR=${CMAKE_INSTALL_INCLUDEDIR}
        ${_build_args}
)

if (MSVC)
    add_debug_dep(dep_Qhull)
endif ()