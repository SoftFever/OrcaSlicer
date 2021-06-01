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

    MeasureResult r;
    for (int i = 0; i < 10; ++i) {
        b.start();
        ItsNeighborsWrapper itsn{its, fn(its)};
        b.stop();

        r.t_index_create += b.getElapsedSec();

        b.start();
        its_split(itsn);
        b.stop();

        r.t_split += b.getElapsedSec();
    }

    r.t_index_create /= 10;
    r.t_split /= 10;

    return r;
}

static indexed_triangle_set two_spheres(double detail)
{
    auto sphere1 = its_make_sphere(10., 2 * PI / detail), sphere2 = sphere1;

    its_transform(sphere1, Transform3f{}.translate(Vec3f{-5.f, 0.f, 0.f}));
    its_transform(sphere2, Transform3f{}.translate(Vec3f{5.f, 0.f, 0.f}));

    its_merge(sphere1, sphere2);

    return sphere1;
}

static const std::map<std::string, indexed_triangle_set> ToMeasure = {
    {"simple", its_make_cube(10., 10., 10.) }, // this has 12 faces, 8 vertices
    {"two_spheres_1x", two_spheres(60.)},
    {"two_spheres_2x", two_spheres(120.)},
    {"two_spheres_4x", two_spheres(240.)},
    {"two_spheres_8x", two_spheres(480.)},
};

static const auto IndexFunctions = std::make_tuple(
    std::make_pair("tamas's unordered_map based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_1); }),
    std::make_pair("vojta std::sort based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_2); }),
    std::make_pair("vojta tbb::parallel_sort based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_3); }),
    std::make_pair("filip's vertex->face based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_5); }),
    std::make_pair("tamas's std::sort based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_6); }),
    std::make_pair("tamas's tbb::parallel_sort based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_7); }),
    std::make_pair("tamas's map based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_8); }),
    std::make_pair("TriangleMesh split", [](const auto &its) {
        TriangleMesh m{its};

        MeasureResult ret;
        for (int i = 0; i < 10; ++i) {
            Benchmark b;
            b.start();
            m.repair();
            m.split();
            b.stop();
            ret.t_split += b.getElapsedSec();
        }
        ret.t_split /= 10;

        return ret;
    })
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
            MeasureResult r = e.second(mesh);
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
