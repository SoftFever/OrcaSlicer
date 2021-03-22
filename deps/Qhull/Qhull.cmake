include(GNUInstallDirs)
prusaslicer_add_cmake_project(Qhull
    # URL "https://github.com/qhull/qhull/archive/v7.3.2.tar.gz"
    # URL_HASH SHA256=619c8a954880d545194bc03359404ef36a1abd2dde03678089459757fd790cb0
    GIT_REPOSITORY  https://github.com/qhull/qhull.git
    GIT_TAG         7afedcc73666e46a9f1d74632412ebecf53b1b30 # v7.3.2 plus the mac build patch
    CMAKE_ARGS 
        -DINCLUDE_INSTALL_DIR=${CMAKE_INSTALL_INCLUDEDIR}
)

if (MSVC)
    add_debug_dep(dep_Qhull)
endif ()