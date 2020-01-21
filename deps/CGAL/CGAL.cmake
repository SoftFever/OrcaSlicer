prusaslicer_add_cmake_project(
    CGAL
    GIT_REPOSITORY https://github.com/CGAL/cgal.git
    GIT_TAG        bec70a6d52d8aacb0b3d82a7b4edc3caa899184b # releases/CGAL-5.0
    # For whatever reason, this keeps downloading forever (repeats downloads if finished)
    # URL      https://github.com/CGAL/cgal/archive/releases/CGAL-5.0.zip
    # URL_HASH SHA256=bd9327be903ab7ee379a8a7a0609eba0962f5078d2497cf8e13e8e1598584154
    DEPENDS dep_boost dep_GMP dep_MPFR
)

include(GNUInstallDirs)

# CGAL, for whatever reason, makes itself non-relocatable by writing the build directory into
# CGALConfig-installation-dirs.cmake and including it in configure time.
# If this file is not present, it will not consider the stored absolute path
ExternalProject_Add_Step(dep_CGAL dep_CGAL_relocation_fix
    DEPENDEES install
    COMMAND ${CMAKE_COMMAND} -E remove CGALConfig-installation-dirs.cmake
    WORKING_DIRECTORY "${DESTDIR}/usr/local/${CMAKE_INSTALL_LIBDIR}/cmake/CGAL"
)