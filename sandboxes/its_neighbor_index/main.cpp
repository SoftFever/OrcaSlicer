#include <iostream>
#include <fstream>
#include <vector>
#include <tuple>

#include "ItsNeighborIndex.hpp"

#include "libnest2d/tools/benchmark.h"
#include "libnest2d/utils/metaloop.hpp"

namespace Slic3r {

struct MeasureResult
{
    double t_index_create = 0;
    double t_split = 0;
    double memory = 0;

    double full_time() const { return t_index_create + t_split; }
};

template<class IndexCreatorFn>
static MeasureResult measure_index(const indexed_triangle_set &its, IndexCreatorFn fn)
{
    Benchmark b;

    b.start();
    ItsNeighborsWrapper itsn{its, fn(its)};
    b.stop();

    MeasureResult r;
    r.t_index_create = b.getElapsedSec();

    b.start();
    its_split(itsn);
    b.stop();

    r.t_split = b.getElapsedSec();

    return r;
}

static TriangleMesh two_spheres(double detail)
{
    TriangleMesh sphere1 = make_sphere(10., 2 * PI / detail), sphere2 = sphere1;

    sphere1.translate(-5.f, 0.f, 0.f);
    sphere2.translate( 5.f, 0.f, 0.f);

    sphere1.merge(sphere2);
    sphere1.require_shared_vertices();

    return sphere1;
}

static const std::map<std::string, TriangleMesh> ToMeasure = {
    {"simple", make_cube(10., 10., 10.) },
    {"two_spheres", two_spheres(200.)},
    {"two_spheres_detail", two_spheres(360.)},
    {"two_spheres_high_detail", two_spheres(3600.)},
};

static const auto IndexFunctions = std::make_tuple(
    std::make_pair("tamas's unordered_map based", its_create_neighbors_index_1),
    std::make_pair("vojta std::sort based", its_create_neighbors_index_2),
    std::make_pair("vojta tbb::parallel_sort based", its_create_neighbors_index_3),
    std::make_pair("filip's vertex->face based", its_create_neighbors_index_5),
    std::make_pair("tamas's std::sort based", its_create_neighbors_index_6),
    std::make_pair("tamas's tbb::parallel_sort based", its_create_neighbors_index_7),
    std::make_pair("tamas's map based", its_create_neighbors_index_8)
);

static constexpr size_t IndexFuncNum = std::tuple_size_v<decltype (IndexFunctions)>;

} // namespace Slic3r

int main(const int argc, const char * argv[])
{
    using namespace Slic3r;

    std::map<std::string, std::array<MeasureResult, IndexFuncNum> > results;
    std::array<std::string, IndexFuncNum> funcnames;

    for (auto &m : ToMeasure) {
        auto &name = m.first;
        auto &mesh = m.second;
        libnest2d::opt::metaloop::apply([&mesh, &name, &results, &funcnames](int N, auto &e) {
            MeasureResult r = measure_index(mesh.its, e.second);
            funcnames[N] = e.first;
            results[name][N] = r;
        }, IndexFunctions);
    }


    std::string outfilename = "out.csv";
    std::fstream outfile;
    if (argc > 1) {
        outfilename = argv[1];
        outfile.open(outfilename, std::fstream::out);
        std::cout << outfilename << " will be used" << std::endl;
    }

    std::ostream &out = outfile.is_open() ? outfile : std::cout;

    out << "model;" ;
    for (const std::string &funcname : funcnames) {
        out << funcname << ";";
    }

    out << std::endl;

    for (auto &[name, result] : results) {
        out << name << ";";
        for (auto &r : result)
            out << r.full_time() << ";";

        out << std::endl;
    }

    return 0;
}
