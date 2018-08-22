#include <iostream>
#include <string>
#include <fstream>
//#define DEBUG_EXPORT_NFP

#include <libnest2d.h>

#include "tests/printer_parts.h"
#include "tools/benchmark.h"
#include "tools/svgtools.hpp"
#include "libnest2d/rotfinder.hpp"

//#include "tools/libnfpglue.hpp"
//#include "tools/nfp_svgnest_glue.hpp"


using namespace libnest2d;
using ItemGroup = std::vector<std::reference_wrapper<Item>>;

std::vector<Item>& _parts(std::vector<Item>& ret, const TestData& data)
{
    if(ret.empty()) {
        ret.reserve(data.size());
        for(auto& inp : data)
            ret.emplace_back(inp);
    }

    return ret;
}

std::vector<Item>& prusaParts() {
    static std::vector<Item> ret;
    return _parts(ret, PRINTER_PART_POLYGONS);
}

std::vector<Item>& stegoParts() {
    static std::vector<Item> ret;
    return _parts(ret, STEGOSAUR_POLYGONS);
}

std::vector<Item>& prusaExParts() {
    static std::vector<Item> ret;
    if(ret.empty()) {
        ret.reserve(PRINTER_PART_POLYGONS_EX.size());
        for(auto& p : PRINTER_PART_POLYGONS_EX) {
            ret.emplace_back(p.Contour, p.Holes);
        }
    }
    return ret;
}

void arrangeRectangles() {
    using namespace libnest2d;

    const int SCALE = 1000000;

    std::vector<Item> rects(202,       {
                                     {-9945219, -3065619},
                                     {-9781479, -2031780},
                                     {-9510560, -1020730},
                                     {-9135450, -43529},
                                     {-2099999, 14110899},
                                     {2099999, 14110899},
                                     {9135450, -43529},
                                     {9510560, -1020730},
                                     {9781479, -2031780},
                                     {9945219, -3065619},
                                     {10000000, -4110899},
                                     {9945219, -5156179},
                                     {9781479, -6190019},
                                     {9510560, -7201069},
                                     {9135450, -8178270},
                                     {8660249, -9110899},
                                     {8090169, -9988750},
                                     {7431449, -10802209},
                                     {6691309, -11542349},
                                     {5877850, -12201069},
                                     {5000000, -12771149},
                                     {4067369, -13246350},
                                     {3090169, -13621459},
                                     {2079119, -13892379},
                                     {1045279, -14056119},
                                     {0, -14110899},
                                     {-1045279, -14056119},
                                     {-2079119, -13892379},
                                     {-3090169, -13621459},
                                     {-4067369, -13246350},
                                     {-5000000, -12771149},
                                     {-5877850, -12201069},
                                     {-6691309, -11542349},
                                     {-7431449, -10802209},
                                     {-8090169, -9988750},
                                     {-8660249, -9110899},
                                     {-9135450, -8178270},
                                     {-9510560, -7201069},
                                     {-9781479, -6190019},
                                     {-9945219, -5156179},
                                     {-10000000, -4110899},
                                     {-9945219, -3065619},
                                 });

    std::vector<Item> input;
    input.insert(input.end(), prusaParts().begin(), prusaParts().end());
//    input.insert(input.end(), prusaExParts().begin(), prusaExParts().end());
//    input.insert(input.end(), stegoParts().begin(), stegoParts().end());
//    input.insert(input.end(), rects.begin(), rects.end());

    Box bin(250*SCALE, 210*SCALE);
//    PolygonImpl bin = {
//        {
//            {25*SCALE, 0},
//            {0, 25*SCALE},
//            {0, 225*SCALE},
//            {25*SCALE, 250*SCALE},
//            {225*SCALE, 250*SCALE},
//            {250*SCALE, 225*SCALE},
//            {250*SCALE, 25*SCALE},
//            {225*SCALE, 0},
//            {25*SCALE, 0}
//        },
//        {}
//    };

//    Circle bin({0, 0}, 125*SCALE);

    auto min_obj_distance = static_cast<Coord>(6*SCALE);

    using Placer = placers::_NofitPolyPlacer<PolygonImpl, decltype(bin)>;
    using Packer = Nester<Placer, FirstFitSelection>;

    Packer arrange(bin, min_obj_distance);

    Packer::PlacementConfig pconf;
    pconf.alignment = Placer::Config::Alignment::CENTER;
    pconf.starting_point = Placer::Config::Alignment::CENTER;
    pconf.rotations = {0.0/*, Pi/2.0, Pi, 3*Pi/2*/};
    pconf.accuracy = 0.65f;
    pconf.parallel = true;

    Packer::SelectionConfig sconf;
//    sconf.allow_parallel = false;
//    sconf.force_parallel = false;
//    sconf.try_triplets = true;
//    sconf.try_reverse_order = true;
//    sconf.waste_increment = 0.01;

    arrange.configure(pconf, sconf);

    arrange.progressIndicator([&](unsigned r){
        std::cout << "Remaining items: " << r << std::endl;
    });

//    findMinimumBoundingBoxRotations(input.begin(), input.end());

    Benchmark bench;

    bench.start();
    Packer::ResultType result;

    try {
        result = arrange.execute(input.begin(), input.end());
    } catch(GeometryException& ge) {
        std::cerr << "Geometry error: " << ge.what() << std::endl;
        return ;
    } catch(std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return ;
    }

    bench.stop();

    std::vector<double> eff;
    eff.reserve(result.size());

    auto bin_area = sl::area(bin);
    for(auto& r : result) {
        double a = 0;
        std::for_each(r.begin(), r.end(), [&a] (Item& e ){ a += e.area(); });
        eff.emplace_back(a/bin_area);
    };

    std::cout << bench.getElapsedSec() << " bin count: " << result.size()
              << std::endl;

    std::cout << "Bin efficiency: (";
    for(double e : eff) std::cout << e*100.0 << "% ";
    std::cout << ") Average: "
              << std::accumulate(eff.begin(), eff.end(), 0.0)*100.0/result.size()
              << " %" << std::endl;

    std::cout << "Bin usage: (";
    size_t total = 0;
    for(auto& r : result) { std::cout << r.size() << " "; total += r.size(); }
    std::cout << ") Total: " << total << std::endl;

//    for(auto& it : input) {
//        auto ret = sl::isValid(it.transformedShape());
//        std::cout << ret.second << std::endl;
//    }

    if(total != input.size()) std::cout << "ERROR " << "could not pack "
                                        << input.size() - total << " elements!"
                                        << std::endl;

    using SVGWriter = svg::SVGWriter<PolygonImpl>;

    SVGWriter::Config conf;
    conf.mm_in_coord_units = SCALE;
    SVGWriter svgw(conf);
    svgw.setSize(Box(250*SCALE, 210*SCALE));
    svgw.writePackGroup(result);
    svgw.save("out");
}

int main(void /*int argc, char **argv*/) {
    arrangeRectangles();
    return EXIT_SUCCESS;
}
