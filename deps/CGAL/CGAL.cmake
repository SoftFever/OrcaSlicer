if (IN_GIT_REPO)
    set(CGAL_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_CGAL-prefix/src/dep_CGAL)
endif ()

orcaslicer_add_cmake_project(
    CGAL
    # GIT_REPOSITORY https://github.com/CGAL/cgal.git
    # GIT_TAG        bec70a6d52d8aacb0b3d82a7b4edc3caa899184b # releases/CGAL-5.0
    # For whatever reason, this keeps downloading forever (repeats downloads if finished)
    URL      https://github.com/CGAL/cgal/archive/refs/tags/v5.6.2.zip
    URL_HASH SHA256=29acaeee5a76a95029fac23131bb1c3a4a75df9a0e7e43b465a1f32d0628f45d
    DEPENDS dep_Boost dep_GMP dep_MPFR
)

include(GNUInstallDirs)
