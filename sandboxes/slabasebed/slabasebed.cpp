#include <iostream>
#include <fstream>
#include <string>

#include <libslic3r/libslic3r.h>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/SLABasePool.hpp>
#include <libslic3r/SLA/SLABoilerPlate.hpp>
#include <libnest2d/tools/benchmark.h>

const std::string USAGE_STR = {
    "Usage: slabasebed stlfilename.stl"
};

namespace Slic3r { namespace sla {

Contour3D convert(const Polygons& triangles, coord_t z, bool dir);
Contour3D walls(const ExPolygon& floor_plate, const ExPolygon& ceiling,
                double floor_z_mm, double ceiling_z_mm,
                ThrowOnCancel thr, double offset_difference_mm = 0.0);

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

    ExPolygon bottom_plate = ground_slice.front();
    ExPolygon top_plate = bottom_plate;
    sla::offset(top_plate, coord_t(3.0/SCALING_FACTOR));
    sla::offset(bottom_plate, coord_t(1.0/SCALING_FACTOR));

    bench.start();

    Polygons top_plate_triangles, bottom_plate_triangles;
    top_plate.triangulate_p2t(&top_plate_triangles);
    bottom_plate.triangulate_p2t(&bottom_plate_triangles);

    auto top_plate_mesh = sla::convert(top_plate_triangles, coord_t(3.0/SCALING_FACTOR), false);
    auto bottom_plate_mesh = sla::convert(bottom_plate_triangles, 0, true);

    mesh.merge(bottom_plate_mesh);
    mesh.merge(top_plate_mesh);

    sla::Contour3D w = sla::walls(bottom_plate, top_plate, 0, 3, [](){}, 2.0);

    mesh.merge(w);
//    sla::create_base_pool(ground_slice, basepool);
    bench.stop();

    cout << "Base pool creation time: " << std::setprecision(10)
         << bench.getElapsedSec() << " seconds." << endl;

//    basepool.write_ascii("out.stl");

    std::fstream outstream("out.obj", std::fstream::out);
    mesh.to_obj(outstream);

    return EXIT_SUCCESS;
}
