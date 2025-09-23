include(GNUInstallDirs)
orcaslicer_add_cmake_project(Qhull
    URL "https://github.com/qhull/qhull/archive/v8.1-alpha6.zip"
    URL_HASH SHA256=d79b73774236f82e4940ce74c8b6cbb6ef3c72ef053d01d1bbfb19ab65dbfc22
    CMAKE_ARGS 
        -DINCLUDE_INSTALL_DIR=${CMAKE_INSTALL_INCLUDEDIR}
)

if (MSVC)
    add_debug_dep(dep_Qhull)
endif ()
