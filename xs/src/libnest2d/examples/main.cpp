#include <iostream>
#include <string>
#include <fstream>

//#define DEBUG_EXPORT_NFP

#include <libnest2d.h>

#include "tests/printer_parts.h"
#include "tools/benchmark.h"
#include "tools/svgtools.hpp"
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

//    Box bin(250*SCALE, 210*SCALE);
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

    _Circle<PointImpl> bin({0, 0}, 125*SCALE);

    auto min_obj_distance = static_cast<Coord>(0*SCALE);

    using Placer = strategies::_NofitPolyPlacer<PolygonImpl, decltype(bin)>;
    using Packer = Arranger<Placer, FirstFitSelection>;

    Packer arrange(bin, min_obj_distance);

    Packer::PlacementConfig pconf;
    pconf.alignment = Placer::Config::Alignment::CENTER;
    pconf.starting_point = Placer::Config::Alignment::CENTER;
    pconf.rotations = {0.0/*, Pi/2.0, Pi, 3*Pi/2*/};
    pconf.accuracy = 1.0f;

//    auto bincenter = ShapeLike::boundingBox<PolygonImpl>(bin).center();
//    pconf.object_function = [&bin, bincenter](
//            Placer::Pile pile, const Item& item,
//            double /*area*/, double norm, double penality) {

//        using pl = PointLike;

//        static const double BIG_ITEM_TRESHOLD = 0.2;
//        static const double GRAVITY_RATIO = 0.5;
//        static const double DENSITY_RATIO = 1.0 - GRAVITY_RATIO;

//        // We will treat big items (compared to the print bed) differently
//        NfpPlacer::Pile bigs;
//        bigs.reserve(pile.size());
//        for(auto& p : pile) {
//            auto pbb = ShapeLike::boundingBox(p);
//            auto na = std::sqrt(pbb.width()*pbb.height())/norm;
//            if(na > BIG_ITEM_TRESHOLD) bigs.emplace_back(p);
//        }

//        // Candidate item bounding box
//        auto ibb = item.boundingBox();

//        // Calculate the full bounding box of the pile with the candidate item
//        pile.emplace_back(item.transformedShape());
//        auto fullbb = ShapeLike::boundingBox(pile);
//        pile.pop_back();

//        // The bounding box of the big items (they will accumulate in the center
//        // of the pile
//        auto bigbb = bigs.empty()? fullbb : ShapeLike::boundingBox(bigs);

//        // The size indicator of the candidate item. This is not the area,
//        // but almost...
//        auto itemnormarea = std::sqrt(ibb.width()*ibb.height())/norm;

//        // Will hold the resulting score
//        double score = 0;

//        if(itemnormarea > BIG_ITEM_TRESHOLD) {
//            // This branch is for the bigger items..
//            // Here we will use the closest point of the item bounding box to
//            // the already arranged pile. So not the bb center nor the a choosen
//            // corner but whichever is the closest to the center. This will
//            // prevent unwanted strange arrangements.

//            auto minc = ibb.minCorner(); // bottom left corner
//            auto maxc = ibb.maxCorner(); // top right corner

//            // top left and bottom right corners
//            auto top_left = PointImpl{getX(minc), getY(maxc)};
//            auto bottom_right = PointImpl{getX(maxc), getY(minc)};

//            auto cc = fullbb.center(); // The gravity center

//            // Now the distnce of the gravity center will be calculated to the
//            // five anchor points and the smallest will be chosen.
//            std::array<double, 5> dists;
//            dists[0] = pl::distance(minc, cc);
//            dists[1] = pl::distance(maxc, cc);
//            dists[2] = pl::distance(ibb.center(), cc);
//            dists[3] = pl::distance(top_left, cc);
//            dists[4] = pl::distance(bottom_right, cc);

//            auto dist = *(std::min_element(dists.begin(), dists.end())) / norm;

//            // Density is the pack density: how big is the arranged pile
//            auto density = std::sqrt(fullbb.width()*fullbb.height()) / norm;

//            // The score is a weighted sum of the distance from pile center
//            // and the pile size
//            score = GRAVITY_RATIO * dist + DENSITY_RATIO * density;

//        } else if(itemnormarea < BIG_ITEM_TRESHOLD && bigs.empty()) {
//            // If there are no big items, only small, we should consider the
//            // density here as well to not get silly results
//            auto bindist = pl::distance(ibb.center(), bincenter) / norm;
//            auto density = std::sqrt(fullbb.width()*fullbb.height()) / norm;
//            score = GRAVITY_RATIO * bindist + DENSITY_RATIO * density;
//        } else {
//            // Here there are the small items that should be placed around the
//            // already processed bigger items.
//            // No need to play around with the anchor points, the center will be
//            // just fine for small items
//            score = pl::distance(ibb.center(), bigbb.center()) / norm;
//        }

//        if(!Placer::wouldFit(fullbb, bin)) score += norm;

//        return score;
//    };

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
    })/*.useMinimumBoundigBoxRotation()*/;

    Benchmark bench;

    bench.start();
    Packer::ResultType result;

    try {
        result = arrange.arrange(input.begin(), input.end());
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

    auto bin_area = ShapeLike::area<PolygonImpl>(bin);
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
        auto ret = ShapeLike::isValid(it.transformedShape());
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
//    findDegenerateCase();

    return EXIT_SUCCESS;
}
