
find_package(CGAL REQUIRED)
add_executable(meshboolean MeshBoolean.cpp)

target_link_libraries(meshboolean libslic3r CGAL::CGAL)