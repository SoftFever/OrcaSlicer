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
        auto res = its_split(itsn);
        b.stop();

        if (res.size() != 2 || res[0].indices.size() != res[1].indices.size() )
            std::cerr << "Something is wrong, split result invalid" << std::endl;

        r.t_split += b.getElapsedSec();
    }

    r.t_index_create /= 10;
    r.t_split /= 10;

    return r;
}

static indexed_triangle_set two_spheres(double detail)
{
    auto sphere1 = its_make_sphere(10., 2 * PI / detail), sphere2 = sphere1;

    its_transform(sphere1, identity3f().translate(Vec3f{-5.f, 0.f, 0.f}));
    its_transform(sphere2, identity3f().translate(Vec3f{5.f, 0.f, 0.f}));

    its_merge(sphere1, sphere2);

    return sphere1;
}

constexpr double sq2 = std::sqrt(2.);

static const std::pair<const std::string, indexed_triangle_set> ToMeasure[] = {
    {"two_spheres_1x", two_spheres(60.)},
    {"two_spheres_2x", two_spheres(120.)},
    {"two_spheres_4x", two_spheres(240.)},
    {"two_spheres_8x", two_spheres(480.)},
    {"two_spheres_16x", two_spheres(2 * 480.)},
    {"two_spheres_32x", two_spheres(2 * 2 * 480.)},

//    {"two_spheres_1x", two_spheres(60.)},
//    {"two_spheres_2x", two_spheres(sq2 * 60.)},
//    {"two_spheres_4x", two_spheres(2 * 60.)},
//    {"two_spheres_8x", two_spheres(sq2 * 2. * 60.)},
//    {"two_spheres_16x", two_spheres(4. * 60.)},
//    {"two_spheres_32x", two_spheres(sq2 * 4. * 60.)},
};

static const auto IndexFunctions = std::make_tuple(
    std::make_pair("tamas's unordered_map based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_1); }),
    std::make_pair("vojta std::sort based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_2); }),
    std::make_pair("vojta tbb::parallel_sort based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_3); }),
    std::make_pair("filip's vertex->face based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_5); }),
    std::make_pair("vojta's vertex->face", [](const auto &its) { return measure_index(its, its_create_neighbors_index_9); }),
    std::make_pair("tamas's std::sort based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_6); }),
    std::make_pair("tamas's tbb::parallel_sort based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_7); }),
    std::make_pair("tamas's map based", [](const auto &its) { return measure_index(its, its_create_neighbors_index_8); })/*,
    std::make_pair("TriangleMesh split", [](const auto &its) {

        MeasureResult ret;
        for (int i = 0; i < 10; ++i) {
            TriangleMesh m{its};
            Benchmark b;
            b.start();
            m.repair();
            auto res = m.split();
            b.stop();

            if (res.size() != 2 || res[0]->size() != res[1]->size())
                std::cerr << "Something is wrong, split result invalid" << std::endl;

            ret.t_split += b.getElapsedSec();
        }
        ret.t_split /= 10;

        return ret;
    })*/

//    std::make_pair("Vojta's vertex->face index", [](const auto &its){
//        Benchmark b;
//        b.start();
//        auto index = create_vertex_faces_index(its);
//        b.stop();

//        if (index.size() != its.vertices.size())
//            std::cerr << "Something went wrong!";

//        return  MeasureResult{b.getElapsedSec(), 0., 0.};
//    }),
//    std::make_pair("Tamas's vertex->face index", [](const auto &its){
//        Benchmark b;
//        b.start();
//        VertexFaceIndex index{its};
//        b.stop();

//        if (index.size() < its.vertices.size())
//            std::cerr << "Something went wrong!";

//        return  MeasureResult{b.getElapsedSec(), 0., 0.};
//    })
);

static constexpr size_t IndexFuncNum = std::tuple_size_v<decltype (IndexFunctions)>;

} // namespace Slic3r

int main(const int argc, const char * argv[])
{
    using namespace Slic3r;

    std::array<MeasureResult, IndexFuncNum> results[std::size(ToMeasure)];
    std::array<std::string, IndexFuncNum> funcnames;

    for (size_t i = 0; i < std::size(ToMeasure); ++i) {
        auto &m = ToMeasure[i];
        auto &name = m.first;
        auto &mesh = m.second;
        std::cout << "Mesh " << name << " has " << mesh.indices.size() << " faces and " << mesh.vertices.size() << " vertices." << std::endl;
        libnest2d::opt::metaloop::apply([&mesh, i, &results, &funcnames](int N, auto &e) {
            MeasureResult r = e.second(mesh);
            funcnames[N] = e.first;
            results[i][N] = r;
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

    for (size_t i = 0; i < std::size(ToMeasure); ++i) {
        const auto &result_row = results[i];
        const std::string &name = ToMeasure[i].first;
        out << name << ";";
        for (auto &r : result_row)
            out << r.t_index_create << ";";

        out << std::endl;
    }

    return 0;
}
