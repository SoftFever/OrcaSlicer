#include <iostream>
#include <string>

#include <libslic3r.h>
#include "TriangleMesh.hpp"
#include "SLABasePool.hpp"
#include "benchmark.h"

const std::string USAGE_STR = {
    "Usage: slabasebed stlfilename.stl"
};

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
    TriangleMesh basepool;

    sla::ground_layer(model, ground_slice, 0.1f);

    bench.start();
    sla::create_base_pool(ground_slice, basepool);
    bench.stop();

    cout << "Base pool creation time: " << std::setprecision(10)
         << bench.getElapsedSec() << " seconds." << endl;

    basepool.write_ascii("out.stl");

    return EXIT_SUCCESS;
}
