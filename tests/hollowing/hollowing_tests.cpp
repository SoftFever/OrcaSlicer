#include <iostream>
#include <catch_main.hpp>

#include <openvdb/openvdb.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h>
#include <libslic3r/TriangleMesh.hpp>
#include "libslic3r/Format/OBJ.hpp"

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR R"(\)"
#else
#define PATH_SEPARATOR R"(/)"
#endif

class TriangleMeshDataAdapter {
public:
    Slic3r::TriangleMesh mesh;
    
    size_t polygonCount() const { return mesh.its.indices.size(); }
    size_t pointCount() const   { return mesh.its.vertices.size(); }
    size_t vertexCount(size_t) const { return 3; }
    
    // Return position pos in local grid index space for polygon n and vertex v
    void getIndexSpacePoint(size_t n, size_t v, openvdb::Vec3d& pos) const {
        auto vidx = size_t(mesh.its.indices[n](Eigen::Index(v)));
        Slic3r::Vec3d p = mesh.its.vertices[vidx].cast<double>();
        pos = {double(p.x()), double(p.y()), p.z()};
    }
};

static Slic3r::TriangleMesh load_model(const std::string &obj_filename)
{
    Slic3r::TriangleMesh mesh;
    auto fpath = TEST_DATA_DIR PATH_SEPARATOR + obj_filename;
    Slic3r::load_obj(fpath.c_str(), &mesh);
    return mesh;
}

TEST_CASE("Load object", "[Hollowing]") {
    TriangleMeshDataAdapter mesh{load_model("20mm_cube.obj")};
    auto ptr = openvdb::tools::meshToVolume<openvdb::FloatGrid>(mesh, {});
    
    REQUIRE(ptr);
    
    std::vector<openvdb::Vec3s> points;
    std::vector<openvdb::Vec4I> quad_indices;
    std::vector<openvdb::Vec3I> triangle_indices;
    
    openvdb::tools::volumeToMesh(*ptr, points, triangle_indices, quad_indices, 0.0, 1.0, true);
    
    std::cout << "Triangle count: " << triangle_indices.size() << std::endl;
    std::cout << "Quad count: " << quad_indices.size() << std::endl;
    std::cout << "Point count: " << points.size() << " vs " << mesh.mesh.its.vertices.size() << std::endl;
}

//int main(int argc, char **argv)
//{
//    ::testing::InitGoogleTest(&argc, argv);
//    return RUN_ALL_TESTS();
//}
