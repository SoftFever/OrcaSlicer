if (IN_GIT_REPO)
    set(CGAL_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_CGAL-prefix/src/dep_CGAL)
endif ()

orcaslicer_add_cmake_project(
    CGAL
    # GIT_REPOSITORY https://github.com/CGAL/cgal.git
    # GIT_TAG        bec70a6d52d8aacb0b3d82a7b4edc3caa899184b # releases/CGAL-6.0.2
    # For whatever reason, this keeps downloading forever (repeats downloads if finished)
    URL      https://github.com/CGAL/cgal/releases/download/v6.0.2/CGAL-6.0.2.zip
    URL_HASH SHA256=1b63ecf4506c6744c5c95196df9e6f8742bae131d6b312089c826048859ee1e8
    PATCH_COMMAND git apply ${CGAL_DIRECTORY_FLAG} --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-clang19.patch
    DEPENDS dep_Boost dep_GMP dep_MPFR
)

include(GNUInstallDirs)
