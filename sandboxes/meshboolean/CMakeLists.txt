if (SLIC3R_STATIC)
    set(CGAL_Boost_USE_STATIC_LIBS ON)
endif ()

find_package(CGAL REQUIRED)

add_executable(meshboolean MeshBoolean.cpp)

target_link_libraries(meshboolean libslic3r CGAL::CGAL)

if (WIN32)
    prusaslicer_copy_dlls(meshboolean)
endif()
