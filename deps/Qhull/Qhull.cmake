include(GNUInstallDirs)
prusaslicer_add_cmake_project(Qhull
    URL "https://github.com/qhull/qhull/archive/v8.0.0.zip"
    URL_HASH SHA256=7edae57142989690bc432893da47db5b7069a150c82ba113e46977904ca326ef
    CMAKE_ARGS 
        -DINCLUDE_INSTALL_DIR=${CMAKE_INSTALL_INCLUDEDIR}
)

if (MSVC)
    add_debug_dep(dep_Qhull)
endif ()