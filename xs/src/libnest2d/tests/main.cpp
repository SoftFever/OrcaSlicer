#include <iostream>
#include <fstream>
#include <string>

#include <libnest2d.h>
#include <libnest2d/geometries_io.hpp>

#include "printer_parts.h"
#include "benchmark.h"

namespace {
using namespace libnest2d;
using ItemGroup = std::vector<std::reference_wrapper<Item>>;
//using PackGroup = std::vector<ItemGroup>;

template<int SCALE, class Bin >
void exportSVG(PackGroup& result, const Bin& bin) {

    std::string loc = "out";

    static std::string svg_header =
R"raw(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.0//EN" "http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd">
<svg height="500" width="500" xmlns="http://www.w3.org/2000/svg" xmlns:svg="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
)raw";

    int i = 0;
    for(auto r : result) {
        std::fstream out(loc + std::to_string(i) + ".svg", std::fstream::out);
        if(out.is_open()) {
            out << svg_header;
            Item rbin( Rectangle(bin.width(), bin.height()) );
            for(unsigned i = 0; i < rbin.vertexCount(); i++) {
                auto v = rbin.vertex(i);
                setY(v, -getY(v)/SCALE + 500 );
                setX(v, getX(v)/SCALE);
                rbin.setVertex(i, v);
            }
            out << ShapeLike::serialize<Formats::SVG>(rbin.rawShape()) << std::endl;
            for(Item& sh : r) {
                Item tsh(sh.transformedShape());
                for(unsigned i = 0; i < tsh.vertexCount(); i++) {
                    auto v = tsh.vertex(i);
                    setY(v, -getY(v)/SCALE + 500);
                    setX(v, getX(v)/SCALE);
                    tsh.setVertex(i, v);
                }
                out << ShapeLike::serialize<Formats::SVG>(tsh.rawShape()) << std::endl;
            }
            out << "\n</svg>" << std::endl;
        }
        out.close();

        i++;
    }
}

template< int SCALE, class Bin>
void exportSVG(ItemGroup& result, const Bin& bin, int idx) {

    std::string loc = "out";

    static std::string svg_header =
R"raw(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.0//EN" "http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd">
<svg height="500" width="500" xmlns="http://www.w3.org/2000/svg" xmlns:svg="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
)raw";

    int i = idx;
    auto r = result;
//    for(auto r : result) {
        std::fstream out(loc + std::to_string(i) + ".svg", std::fstream::out);
        if(out.is_open()) {
            out << svg_header;
            Item rbin( Rectangle(bin.width(), bin.height()) );
            for(unsigned i = 0; i < rbin.vertexCount(); i++) {
                auto v = rbin.vertex(i);
                setY(v, -getY(v)/SCALE + 500 );
                setX(v, getX(v)/SCALE);
                rbin.setVertex(i, v);
            }
            out << ShapeLike::serialize<Formats::SVG>(rbin.rawShape()) << std::endl;
            for(Item& sh : r) {
                Item tsh(sh.transformedShape());
                for(unsigned i = 0; i < tsh.vertexCount(); i++) {
                    auto v = tsh.vertex(i);
                    setY(v, -getY(v)/SCALE + 500);
                    setX(v, getX(v)/SCALE);
                    tsh.setVertex(i, v);
                }
                out << ShapeLike::serialize<Formats::SVG>(tsh.rawShape()) << std::endl;
            }
            out << "\n</svg>" << std::endl;
        }
        out.close();

//        i++;
//    }
}
}


void findDegenerateCase() {
    using namespace libnest2d;

    auto input = PRINTER_PART_POLYGONS;

    auto scaler = [](Item& item) {
        for(unsigned i = 0; i < item.vertexCount(); i++) {
            auto v = item.vertex(i);
            setX(v, 100*getX(v)); setY(v, 100*getY(v));
            item.setVertex(i, v);
        }
    };

    auto cmp = [](const Item& t1, const Item& t2) {
        return t1.area() > t2.area();
    };

    std::for_each(input.begin(), input.end(), scaler);

    std::sort(input.begin(), input.end(), cmp);

    Box bin(210*100, 250*100);
    BottomLeftPlacer placer(bin);

    auto it = input.begin();
    auto next = it;
    int i = 0;
    while(it != input.end() && ++next != input.end()) {
        placer.pack(*it);
        placer.pack(*next);

        auto result = placer.getItems();
        bool valid = true;

        if(result.size() == 2) {
            Item& r1 = result[0];
            Item& r2 = result[1];
            valid = !Item::intersects(r1, r2) || Item::touches(r1, r2);
            valid = (valid && !r1.isInside(r2) && !r2.isInside(r1));
            if(!valid) {
                std::cout << "error index: " << i << std::endl;
                exportSVG<100>(result, bin, i);
            }
        } else {
            std::cout << "something went terribly wrong!" << std::endl;
        }


        placer.clearItems();
        it++;
        i++;
    }
}

void arrangeRectangles() {
    using namespace libnest2d;


//    std::vector<Rectangle> input = {
//        {80, 80},
//        {110, 10},
//        {200, 5},
//        {80, 30},
//        {60, 90},
//        {70, 30},
//        {80, 60},
//        {60, 60},
//        {60, 40},
//        {40, 40},
//        {10, 10},
//        {10, 10},
//        {10, 10},
//        {10, 10},
//        {10, 10},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {20, 20},
//        {80, 80},
//        {110, 10},
//        {200, 5},
//        {80, 30},
//        {60, 90},
//        {70, 30},
//        {80, 60},
//        {60, 60},
//        {60, 40},
//        {40, 40},
//        {10, 10},
//        {10, 10},
//        {10, 10},
//        {10, 10},
//        {10, 10},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {5, 5},
//        {20, 20}
//    };

    auto input = PRINTER_PART_POLYGONS;

    const int SCALE = 1000000;
//    const int SCALE = 1;

    Box bin(210*SCALE, 250*SCALE);

    auto scaler = [&SCALE, &bin](Item& item) {
//        double max_area = 0;
        for(unsigned i = 0; i < item.vertexCount(); i++) {
            auto v = item.vertex(i);
            setX(v, SCALE*getX(v)); setY(v, SCALE*getY(v));
            item.setVertex(i, v);
//            double area = item.area();
//            if(max_area < area) {
//                max_area = area;
//                bin = item.boundingBox();
//            }
        }
    };

    Coord min_obj_distance = 2*SCALE;

    std::for_each(input.begin(), input.end(), scaler);

    Arranger<BottomLeftPlacer, DJDHeuristic> arrange(bin, min_obj_distance);

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

    exportSVG<SCALE>(result, bin);

}

int main(void /*int argc, char **argv*/) {
    arrangeRectangles();
//    findDegenerateCase();
    return EXIT_SUCCESS;
}
