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

Contour3D create_base_pool(const Polygons &ground_layer, 
                           const Polygons &holes = {},
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

    Polygons ground_slice;
    sla::base_plate(model, ground_slice, 0.1f);
    if(ground_slice.empty()) return EXIT_FAILURE;

    Polygon gndfirst; gndfirst = ground_slice.front();
    sla::offset_with_breakstick_holes(gndfirst, 0.5, 10, 0.3);

    sla::Contour3D mesh;


    bench.start();

    sla::PoolConfig cfg;
    cfg.min_wall_height_mm = 0;
    cfg.edge_radius_mm = 0;
    mesh = sla::create_base_pool(ground_slice, {}, cfg);

    bench.stop();

    cout << "Base pool creation time: " << std::setprecision(10)
         << bench.getElapsedSec() << " seconds." << endl;

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
