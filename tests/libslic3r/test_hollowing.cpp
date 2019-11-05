#include <iostream>
#include <fstream>
#include <catch2/catch.hpp>

#include "libslic3r/OpenVDBUtils.hpp"
#include "libslic3r/Format/OBJ.hpp"

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR R"(\)"
#else
#define PATH_SEPARATOR R"(/)"
#endif

static Slic3r::TriangleMesh load_model(const std::string &obj_filename)
{
    Slic3r::TriangleMesh mesh;
    auto fpath = TEST_DATA_DIR PATH_SEPARATOR + obj_filename;
    Slic3r::load_obj(fpath.c_str(), &mesh);
    return mesh;
}

static bool _check_normals(const Slic3r::sla::Contour3D &mesh)
{
    for (auto & face : mesh.faces3)
    {
        
    }
    
    return false;
}

TEST_CASE("Passing OpenVDB grid conversion produce similar geometry.", "[Hollowing]")
{
    Slic3r::TriangleMesh in_mesh = load_model("20mm_cube.obj");
    Slic3r::sla::Contour3D imesh = Slic3r::sla::Contour3D{in_mesh};
    auto ptr = Slic3r::meshToVolume(imesh, {});
    
    REQUIRE(ptr);
    
    std::cout << "Grid class = " << ptr->getGridClass() << std::endl;
    
    Slic3r::sla::Contour3D omesh = Slic3r::volumeToMesh(*ptr, -2.9, 1.0, true);
    
    std::cout << "Triangle count: " << omesh.faces3.size() << std::endl;
    std::cout << "Quad count: " << omesh.faces4.size() << std::endl;
    
    REQUIRE(!omesh.empty());
    
    SECTION("Converting to Contour3D to TriangleMesh") {
        Slic3r::TriangleMesh msh = Slic3r::sla::to_triangle_mesh(omesh);
        
        msh.require_shared_vertices();
        msh.WriteOBJFile("out_tr.obj");
        
        REQUIRE(msh.volume() == Approx(in_mesh.volume()));        
    }
    
//    omesh.faces4.clear();
    std::fstream outfile{"out.obj", std::ios::out};
    omesh.to_obj(outfile);
    
}

//TEST_CASE("Negative 3D offset should produce smaller object.", "[Hollowing]")
//{
//    Slic3r::sla::Contour3D imesh = Slic3r::sla::Contour3D{load_model("20mm_cube.obj")};
//    auto ptr = Slic3r::meshToVolume(imesh, {});
    
//    REQUIRE(ptr);
    
//    Slic3r::sla::Contour3D omesh = Slic3r::volumeToMesh(*ptr, -1., 0.0, true);
    
//    REQUIRE(!omesh.empty());
    
//    imesh.merge(omesh);
//    std::fstream merged_outfile("merged_out.obj", std::ios::out);
//    imesh.to_obj(merged_outfile);
//}
