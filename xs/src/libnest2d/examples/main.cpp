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
//    const int SCALE = 1;
    std::vector<Rectangle> rects = {
        {80*SCALE, 80*SCALE},
        {60*SCALE, 90*SCALE},
        {70*SCALE, 30*SCALE},
        {80*SCALE, 60*SCALE},
        {60*SCALE, 60*SCALE},
        {60*SCALE, 40*SCALE},
        {40*SCALE, 40*SCALE},
        {10*SCALE, 10*SCALE},
        {10*SCALE, 10*SCALE},
        {10*SCALE, 10*SCALE},
        {10*SCALE, 10*SCALE},
        {10*SCALE, 10*SCALE},
        {5*SCALE, 5*SCALE},
        {5*SCALE, 5*SCALE},
        {5*SCALE, 5*SCALE},
        {5*SCALE, 5*SCALE},
        {5*SCALE, 5*SCALE},
        {5*SCALE, 5*SCALE},
        {5*SCALE, 5*SCALE},
        {20*SCALE, 20*SCALE}
       };

//    std::vector<Rectangle> rects = {
//        {20*SCALE, 10*SCALE},
//        {20*SCALE, 10*SCALE},
//        {20*SCALE, 20*SCALE},
//    };

//    std::vector<Item> input {
//        {{0, 0}, {0, 20*SCALE}, {10*SCALE, 0}, {0, 0}}
//    };

    std::vector<Item> crasher =
    {
        {
            {-5000000, 8954050},
            {5000000, 8954050},
            {5000000, -45949},
            {4972609, -568549},
            {3500000, -8954050},
            {-3500000, -8954050},
            {-4972609, -568549},
            {-5000000, -45949},
            {-5000000, 8954050},
        },
        {
            {-5000000, 8954050},
            {5000000, 8954050},
            {5000000, -45949},
            {4972609, -568549},
            {3500000, -8954050},
            {-3500000, -8954050},
            {-4972609, -568549},
            {-5000000, -45949},
            {-5000000, 8954050},
        },
        {
            {-5000000, 8954050},
            {5000000, 8954050},
            {5000000, -45949},
            {4972609, -568549},
            {3500000, -8954050},
            {-3500000, -8954050},
            {-4972609, -568549},
            {-5000000, -45949},
            {-5000000, 8954050},
        },
        {
            {-5000000, 8954050},
            {5000000, 8954050},
            {5000000, -45949},
            {4972609, -568549},
            {3500000, -8954050},
            {-3500000, -8954050},
            {-4972609, -568549},
            {-5000000, -45949},
            {-5000000, 8954050},
        },
        {
            {-5000000, 8954050},
            {5000000, 8954050},
            {5000000, -45949},
            {4972609, -568549},
            {3500000, -8954050},
            {-3500000, -8954050},
            {-4972609, -568549},
            {-5000000, -45949},
            {-5000000, 8954050},
        },
        {
            {-5000000, 8954050},
            {5000000, 8954050},
            {5000000, -45949},
            {4972609, -568549},
            {3500000, -8954050},
            {-3500000, -8954050},
            {-4972609, -568549},
            {-5000000, -45949},
            {-5000000, 8954050},
        },
        {
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
            {9781479, -6190020},
            {9510560, -7201069},
            {9135450, -8178270},
            {8660249, -9110899},
            {8090169, -9988750},
            {7431449, -10802200},
            {6691309, -11542300},
            {5877850, -12201100},
            {5000000, -12771100},
            {4067369, -13246399},
            {3090169, -13621500},
            {2079119, -13892399},
            {1045279, -14056099},
            {0, -14110899},
            {-1045279, -14056099},
            {-2079119, -13892399},
            {-3090169, -13621500},
            {-4067369, -13246399},
            {-5000000, -12771100},
            {-5877850, -12201100},
            {-6691309, -11542300},
            {-7431449, -10802200},
            {-8090169, -9988750},
            {-8660249, -9110899},
            {-9135450, -8178270},
            {-9510560, -7201069},
            {-9781479, -6190020},
            {-9945219, -5156179},
            {-10000000, -4110899},
            {-9945219, -3065619},
        },
        {
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
            {9781479, -6190020},
            {9510560, -7201069},
            {9135450, -8178270},
            {8660249, -9110899},
            {8090169, -9988750},
            {7431449, -10802200},
            {6691309, -11542300},
            {5877850, -12201100},
            {5000000, -12771100},
            {4067369, -13246399},
            {3090169, -13621500},
            {2079119, -13892399},
            {1045279, -14056099},
            {0, -14110899},
            {-1045279, -14056099},
            {-2079119, -13892399},
            {-3090169, -13621500},
            {-4067369, -13246399},
            {-5000000, -12771100},
            {-5877850, -12201100},
            {-6691309, -11542300},
            {-7431449, -10802200},
            {-8090169, -9988750},
            {-8660249, -9110899},
            {-9135450, -8178270},
            {-9510560, -7201069},
            {-9781479, -6190020},
            {-9945219, -5156179},
            {-10000000, -4110899},
            {-9945219, -3065619},
        },
        {
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
            {9781479, -6190020},
            {9510560, -7201069},
            {9135450, -8178270},
            {8660249, -9110899},
            {8090169, -9988750},
            {7431449, -10802200},
            {6691309, -11542300},
            {5877850, -12201100},
            {5000000, -12771100},
            {4067369, -13246399},
            {3090169, -13621500},
            {2079119, -13892399},
            {1045279, -14056099},
            {0, -14110899},
            {-1045279, -14056099},
            {-2079119, -13892399},
            {-3090169, -13621500},
            {-4067369, -13246399},
            {-5000000, -12771100},
            {-5877850, -12201100},
            {-6691309, -11542300},
            {-7431449, -10802200},
            {-8090169, -9988750},
            {-8660249, -9110899},
            {-9135450, -8178270},
            {-9510560, -7201069},
            {-9781479, -6190020},
            {-9945219, -5156179},
            {-10000000, -4110899},
            {-9945219, -3065619},
        },
        {
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
            {9781479, -6190020},
            {9510560, -7201069},
            {9135450, -8178270},
            {8660249, -9110899},
            {8090169, -9988750},
            {7431449, -10802200},
            {6691309, -11542300},
            {5877850, -12201100},
            {5000000, -12771100},
            {4067369, -13246399},
            {3090169, -13621500},
            {2079119, -13892399},
            {1045279, -14056099},
            {0, -14110899},
            {-1045279, -14056099},
            {-2079119, -13892399},
            {-3090169, -13621500},
            {-4067369, -13246399},
            {-5000000, -12771100},
            {-5877850, -12201100},
            {-6691309, -11542300},
            {-7431449, -10802200},
            {-8090169, -9988750},
            {-8660249, -9110899},
            {-9135450, -8178270},
            {-9510560, -7201069},
            {-9781479, -6190020},
            {-9945219, -5156179},
            {-10000000, -4110899},
            {-9945219, -3065619},
        },
        {
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
            {9781479, -6190020},
            {9510560, -7201069},
            {9135450, -8178270},
            {8660249, -9110899},
            {8090169, -9988750},
            {7431449, -10802200},
            {6691309, -11542300},
            {5877850, -12201100},
            {5000000, -12771100},
            {4067369, -13246399},
            {3090169, -13621500},
            {2079119, -13892399},
            {1045279, -14056099},
            {0, -14110899},
            {-1045279, -14056099},
            {-2079119, -13892399},
            {-3090169, -13621500},
            {-4067369, -13246399},
            {-5000000, -12771100},
            {-5877850, -12201100},
            {-6691309, -11542300},
            {-7431449, -10802200},
            {-8090169, -9988750},
            {-8660249, -9110899},
            {-9135450, -8178270},
            {-9510560, -7201069},
            {-9781479, -6190020},
            {-9945219, -5156179},
            {-10000000, -4110899},
            {-9945219, -3065619},
        },
        {
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
            {9781479, -6190020},
            {9510560, -7201069},
            {9135450, -8178270},
            {8660249, -9110899},
            {8090169, -9988750},
            {7431449, -10802200},
            {6691309, -11542300},
            {5877850, -12201100},
            {5000000, -12771100},
            {4067369, -13246399},
            {3090169, -13621500},
            {2079119, -13892399},
            {1045279, -14056099},
            {0, -14110899},
            {-1045279, -14056099},
            {-2079119, -13892399},
            {-3090169, -13621500},
            {-4067369, -13246399},
            {-5000000, -12771100},
            {-5877850, -12201100},
            {-6691309, -11542300},
            {-7431449, -10802200},
            {-8090169, -9988750},
            {-8660249, -9110899},
            {-9135450, -8178270},
            {-9510560, -7201069},
            {-9781479, -6190020},
            {-9945219, -5156179},
            {-10000000, -4110899},
            {-9945219, -3065619},
        },
        {
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
            {9781479, -6190020},
            {9510560, -7201069},
            {9135450, -8178270},
            {8660249, -9110899},
            {8090169, -9988750},
            {7431449, -10802200},
            {6691309, -11542300},
            {5877850, -12201100},
            {5000000, -12771100},
            {4067369, -13246399},
            {3090169, -13621500},
            {2079119, -13892399},
            {1045279, -14056099},
            {0, -14110899},
            {-1045279, -14056099},
            {-2079119, -13892399},
            {-3090169, -13621500},
            {-4067369, -13246399},
            {-5000000, -12771100},
            {-5877850, -12201100},
            {-6691309, -11542300},
            {-7431449, -10802200},
            {-8090169, -9988750},
            {-8660249, -9110899},
            {-9135450, -8178270},
            {-9510560, -7201069},
            {-9781479, -6190020},
            {-9945219, -5156179},
            {-10000000, -4110899},
            {-9945219, -3065619},
        },
        {
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
            {9781479, -6190020},
            {9510560, -7201069},
            {9135450, -8178270},
            {8660249, -9110899},
            {8090169, -9988750},
            {7431449, -10802200},
            {6691309, -11542300},
            {5877850, -12201100},
            {5000000, -12771100},
            {4067369, -13246399},
            {3090169, -13621500},
            {2079119, -13892399},
            {1045279, -14056099},
            {0, -14110899},
            {-1045279, -14056099},
            {-2079119, -13892399},
            {-3090169, -13621500},
            {-4067369, -13246399},
            {-5000000, -12771100},
            {-5877850, -12201100},
            {-6691309, -11542300},
            {-7431449, -10802200},
            {-8090169, -9988750},
            {-8660249, -9110899},
            {-9135450, -8178270},
            {-9510560, -7201069},
            {-9781479, -6190020},
            {-9945219, -5156179},
            {-10000000, -4110899},
            {-9945219, -3065619},
        },
        {
            {-18000000, -1000000},
            {-15000000, 22000000},
            {-11000000, 26000000},
            {11000000, 26000000},
            {15000000, 22000000},
            {18000000, -1000000},
            {18000000, -26000000},
            {-18000000, -26000000},
            {-18000000, -1000000},
        },
    };

    std::vector<Item> proba = {
        {
            Rectangle(100, 2)
        },
        {
            Rectangle(100, 2)
        },
        {
            Rectangle(100, 2)
        },
        {
            Rectangle(10, 10)
        },
    };

    proba[0].rotate(Pi/3);
    proba[1].rotate(Pi-Pi/3);

//    std::vector<Item> input(25, Rectangle(70*SCALE, 10*SCALE));
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

    auto min_obj_distance = static_cast<Coord>(0*SCALE);

    using Placer = strategies::_NofitPolyPlacer<PolygonImpl, Box>;
    using Packer = Arranger<Placer, FirstFitSelection>;

    Packer arrange(bin, min_obj_distance);

    Packer::PlacementConfig pconf;
    pconf.alignment = Placer::Config::Alignment::CENTER;
    pconf.starting_point = Placer::Config::Alignment::CENTER;
    pconf.rotations = {0.0/*, Pi/2.0, Pi, 3*Pi/2*/};
    pconf.accuracy = 0.5f;

//    auto bincenter = ShapeLike::boundingBox(bin).center();
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

//        // If it does not fit into the print bed we will beat it
//        // with a large penality. If we would not do this, there would be only
//        // one big pile that doesn't care whether it fits onto the print bed.
//        if(!NfpPlacer::wouldFit(fullbb, bin)) score = 2*penality - score;

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
