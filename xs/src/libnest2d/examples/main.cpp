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

    std::vector<Rectangle> rects = {
        {60*SCALE, 200*SCALE},
        {60*SCALE, 200*SCALE}
    };

    std::vector<Item> input;
    input.insert(input.end(), prusaParts().begin(), prusaParts().end());
//    input.insert(input.end(), prusaExParts().begin(), prusaExParts().end());
//    input.insert(input.end(), stegoParts().begin(), stegoParts().end());
//    input.insert(input.end(), rects.begin(), rects.end());
//    input.insert(input.end(), proba.begin(), proba.end());
//    input.insert(input.end(), crasher.begin(), crasher.end());

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

//    _Circle<PointImpl> bin({0, 0}, 125*SCALE);

    auto min_obj_distance = static_cast<Coord>(0*SCALE);

    using Placer = strategies::_NofitPolyPlacer<PolygonImpl, decltype(bin)>;
    using Packer = Nester<Placer, FirstFitSelection>;

    Packer arrange(bin, min_obj_distance);

    Packer::PlacementConfig pconf;
    pconf.alignment = Placer::Config::Alignment::CENTER;
    pconf.starting_point = Placer::Config::Alignment::CENTER;
    pconf.rotations = {0.0/*, Pi/2.0, Pi, 3*Pi/2*/};
    pconf.accuracy = 1.0f;

    Packer::SelectionConfig sconf;
//    sconf.allow_parallel = false;
//    sconf.force_parallel = false;
//    sconf.try_triplets = true;
//    sconf.try_reverse_order = true;
//    sconf.waste_increment = 0.005;

    arrange.configure(pconf, sconf);

    arrange.progressIndicator([&](unsigned r){
//        svg::SVGWriter::Config conf;
//        conf.mm_in_coord_units = SCALE;
//        svg::SVGWriter svgw(conf);
//        svgw.setSize(bin);
//        svgw.writePackGroup(arrange.lastResult());
//        svgw.save("debout");
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

    for(auto& it : input) {
        auto ret = sl::isValid(it.transformedShape());
        std::cout << ret.second << std::endl;
    }

    if(total != input.size()) std::cout << "ERROR " << "could not pack "
                                        << input.size() - total << " elements!"
                                        << std::endl;

    using SVGWriter = svg::SVGWriter<PolygonImpl>;

    SVGWriter::Config conf;
    conf.mm_in_coord_units = SCALE;
    SVGWriter svgw(conf);
    svgw.setSize(Box(250*SCALE, 210*SCALE));
    svgw.writePackGroup(result);
//    std::for_each(input.begin(), input.end(), [&svgw](Item& item){ svgw.writeItem(item);});
    svgw.save("out");
}

int main(void /*int argc, char **argv*/) {
    arrangeRectangles();
////    findDegenerateCase();

    return EXIT_SUCCESS;
}
