prusaslicer_add_cmake_project(
    CGAL
    GIT_REPOSITORY https://github.com/CGAL/cgal.git
    GIT_TAG "releases/CGAL-5.0"
    DEPENDS dep_boost dep_GMP dep_MPFR
)