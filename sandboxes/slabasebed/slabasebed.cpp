#include <iostream>
#include <fstream>
#include <string>

#include <libslic3r/libslic3r.h>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/Tesselate.hpp>
#include <libslic3r/SLA/SLABasePool.hpp>
#include <libslic3r/SLA/SLABoilerPlate.hpp>
#include <libnest2d/tools/benchmark.h>

const std::string USAGE_STR = {
    "Usage: slabasebed stlfilename.stl"
};

namespace Slic3r { namespace sla {

Contour3D create_base_pool(const ExPolygons &ground_layer, 
                           const PoolConfig& cfg = PoolConfig());

Contour3D walls(const Polygon& floor_plate, const Polygon& ceiling,
                double floor_z_mm, double ceiling_z_mm,
                double offset_difference_mm, ThrowOnCancel thr);

void offset(ExPolygon& sh, coord_t distance);

}
}

int main(const int argc, const char *argv[]) {
    using namespace Slic3r;
    using std::cout; using std::endl;

    if(argc < 2) {
        cout << USAGE_STR << endl;
        return EXIT_SUCCESS;
    }

    TriangleMesh model;
    Benchmark bench;

    model.ReadSTLFile(argv[1]);
    model.align_to_origin();

    ExPolygons ground_slice;
    sla::Contour3D mesh;
//    TriangleMesh basepool;

    sla::base_plate(model, ground_slice, 0.1f);

    if(ground_slice.empty()) return EXIT_FAILURE;

//    ExPolygon bottom_plate = ground_slice.front();
//    ExPolygon top_plate = bottom_plate;
//    sla::offset(top_plate, coord_t(3.0/SCALING_FACTOR));
//    sla::offset(bottom_plate, coord_t(1.0/SCALING_FACTOR));

    bench.start();

//    TriangleMesh pool;
    sla::PoolConfig cfg;
    cfg.min_wall_height_mm = 0;
    cfg.edge_radius_mm = 0.2;
    mesh = sla::create_base_pool(ground_slice, cfg);
    
//    mesh.merge(triangulate_expolygon_3d(top_plate, 3.0, false));
//    mesh.merge(triangulate_expolygon_3d(bottom_plate, 0.0, true));
//    mesh = sla::walls(bottom_plate.contour, top_plate.contour, 0, 3, 2.0, [](){});
    
    bench.stop();

    cout << "Base pool creation time: " << std::setprecision(10)
         << bench.getElapsedSec() << " seconds." << endl;
    
//    auto point = []()
    for(auto& trind : mesh.indices) {
        Vec3d p0 = mesh.points[size_t(trind[0])];
        Vec3d p1 = mesh.points[size_t(trind[1])];
        Vec3d p2 = mesh.points[size_t(trind[2])];
        Vec3d p01 = p1 - p0;
        Vec3d p02 = p2 - p0;
        auto a = p01.cross(p02).norm() / 2.0;
        if(std::abs(a) < 1e-6) std::cout << "degenerate triangle" << std::endl;
    }

//    basepool.write_ascii("out.stl");

    std::fstream outstream("out.obj", std::fstream::out);
    mesh.to_obj(outstream);

    return EXIT_SUCCESS;
}
