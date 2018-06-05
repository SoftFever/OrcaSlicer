#include <iostream>
#include <fstream>
#include <string>

#include <libnest2d.h>
#include <libnest2d/geometries_io.hpp>

#include "printer_parts.h"
#include "benchmark.h"
#include "svgtools.hpp"

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

void arrangeRectangles() {
    using namespace libnest2d;

    auto input = stegoParts();

    const int SCALE = 1000000;

    Box bin(210*SCALE, 250*SCALE);

    Coord min_obj_distance = 0; //6*SCALE;

    NfpPlacer::Config pconf;
    pconf.alignment = NfpPlacer::Config::Alignment::TOP_LEFT;
    Arranger<NfpPlacer, DJDHeuristic> arrange(bin, min_obj_distance, pconf);

//    arrange.progressIndicator([&arrange, &bin](unsigned r){
//        svg::SVGWriter::Config conf;
//        conf.mm_in_coord_units = SCALE;
//        svg::SVGWriter svgw(conf);
//        svgw.setSize(bin);
//        svgw.writePackGroup(arrange.lastResult());
//        svgw.save("out");
//        std::cout << "Remaining items: " << r << std::endl;
//    });

    Benchmark bench;

    bench.start();
    auto result = arrange(input.begin(),
                          input.end());

    bench.stop();

    std::cout << bench.getElapsedSec() << std::endl;

    for(auto& it : input) {
        auto ret = ShapeLike::isValid(it.transformedShape());
        std::cout << ret.second << std::endl;
    }

    svg::SVGWriter::Config conf;
    conf.mm_in_coord_units = SCALE;
    svg::SVGWriter svgw(conf);
    svgw.setSize(bin);
    svgw.writePackGroup(result);
    svgw.save("out");
}

int main(void /*int argc, char **argv*/) {
    arrangeRectangles();
//    findDegenerateCase();
    return EXIT_SUCCESS;
}
