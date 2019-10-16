#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <fstream>


#include <libnest2d/libnest2d.hpp>
#include "printer_parts.hpp"
//#include <libnest2d/geometry_traits_nfp.hpp>
#include "../tools/svgtools.hpp"
#include <libnest2d/utils/rotcalipers.hpp>

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

#include "boost/multiprecision/integer.hpp"
#include "boost/rational.hpp"

//#include "../tools/libnfpglue.hpp"
//#include "../tools/nfp_svgnest_glue.hpp"

namespace libnest2d {
#if !defined(_MSC_VER) && defined(__SIZEOF_INT128__) && !defined(__APPLE__)
using LargeInt = __int128;
#else
using LargeInt = boost::multiprecision::int128_t;
template<> struct _NumTag<LargeInt> { using Type = ScalarTag; };
#endif
template<class T> struct _NumTag<boost::rational<T>> { using Type = RationalTag; };

using RectangleItem = libnest2d::Rectangle;

namespace nfp {

template<class S>
struct NfpImpl<S, NfpLevel::CONVEX_ONLY>
{
    NfpResult<S> operator()(const S &sh, const S &other)
    {
        return nfpConvexOnly<S, boost::rational<LargeInt>>(sh, other);
    }
};

}
}

static std::vector<libnest2d::Item>& prusaParts() {
    static std::vector<libnest2d::Item> ret;

    if(ret.empty()) {
        ret.reserve(PRINTER_PART_POLYGONS.size());
        for(auto& inp : PRINTER_PART_POLYGONS) ret.emplace_back(inp);
    }

    return ret;
}

TEST_CASE("Angles", "[Geometry]")
{

    using namespace libnest2d;

    Degrees deg(180);
    Radians rad(deg);
    Degrees deg2(rad);

    REQUIRE(Approx(rad) == Pi);
    REQUIRE(Approx(deg) == 180);
    REQUIRE(Approx(deg2) == 180);
    REQUIRE(Approx(rad) == Radians(deg));
    REQUIRE(Approx(Degrees(rad)) == deg);

    REQUIRE(rad == deg);

    Segment seg = {{0, 0}, {12, -10}};

    REQUIRE(Degrees(seg.angleToXaxis()) > 270);
    REQUIRE(Degrees(seg.angleToXaxis()) < 360);

    seg = {{0, 0}, {12, 10}};

    REQUIRE(Degrees(seg.angleToXaxis()) > 0);
    REQUIRE(Degrees(seg.angleToXaxis()) < 90);

    seg = {{0, 0}, {-12, 10}};

    REQUIRE(Degrees(seg.angleToXaxis()) > 90);
    REQUIRE(Degrees(seg.angleToXaxis()) < 180);

    seg = {{0, 0}, {-12, -10}};

    REQUIRE(Degrees(seg.angleToXaxis()) > 180);
    REQUIRE(Degrees(seg.angleToXaxis()) < 270);

    seg = {{0, 0}, {1, 0}};

    REQUIRE(Degrees(seg.angleToXaxis()) == Approx(0.));

    seg = {{0, 0}, {0, 1}};

    REQUIRE(Degrees(seg.angleToXaxis()) == Approx(90.));

    seg = {{0, 0}, {-1, 0}};

    REQUIRE(Degrees(seg.angleToXaxis()) == Approx(180.));

    seg = {{0, 0}, {0, -1}};

    REQUIRE(Degrees(seg.angleToXaxis()) == Approx(270.));
}

// Simple TEST_CASE, does not use gmock
TEST_CASE("ItemCreationAndDestruction", "[Nesting]")
{
    using namespace libnest2d;

    Item sh = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };

    REQUIRE(sh.vertexCount() == 4u);

    Item sh2 ({ {0, 0}, {1, 0}, {1, 1}, {0, 1} });

    REQUIRE(sh2.vertexCount() == 4u);

    // copy
    Item sh3 = sh2;

    REQUIRE(sh3.vertexCount() == 4u);

    sh2 = {};

    REQUIRE(sh2.vertexCount() == 0u);
    REQUIRE(sh3.vertexCount() == 4u);
}

TEST_CASE("boundingCircle", "[Geometry]") {
    using namespace libnest2d;
    using placers::boundingCircle;

    PolygonImpl p = {{{0, 10}, {10, 0}, {0, -10}, {0, 10}}, {}};
    Circle c = boundingCircle(p);

    REQUIRE(c.center().X == 0);
    REQUIRE(c.center().Y == 0);
    REQUIRE(c.radius() == Approx(10));

    shapelike::translate(p, PointImpl{10, 10});
    c = boundingCircle(p);

    REQUIRE(c.center().X == 10);
    REQUIRE(c.center().Y == 10);
    REQUIRE(c.radius() == Approx(10));

    auto parts = prusaParts();

    int i = 0;
    for(auto& part : parts) {
        c = boundingCircle(part.transformedShape());
        if(std::isnan(c.radius())) std::cout << "fail: radius is nan" << std::endl;

        else for(auto v : shapelike::contour(part.transformedShape()) ) {
                auto d = pointlike::distance(v, c.center());
                if(d > c.radius() ) {
                    auto e = std::abs( 1.0 - d/c.radius());
                    REQUIRE(e <= 1e-3);
                }
            }
        i++;
    }

}

TEST_CASE("Distance", "[Geometry]") {
    using namespace libnest2d;

    Point p1 = {0, 0};

    Point p2 = {10, 0};
    Point p3 = {10, 10};

    REQUIRE(pointlike::distance(p1, p2) == Approx(10));
    REQUIRE(pointlike::distance(p1, p3) == Approx(sqrt(200)));

    Segment seg(p1, p3);

    //    REQUIRE(pointlike::distance(p2, seg) == Approx(7.0710678118654755));

    auto result = pointlike::horizontalDistance(p2, seg);

    auto check = [](TCompute<Coord> val, TCompute<Coord> expected) {
        if(std::is_floating_point<TCompute<Coord>>::value)
            REQUIRE(static_cast<double>(val) ==
                    Approx(static_cast<double>(expected)));
        else
            REQUIRE(val == expected);
    };

    REQUIRE(result.second);
    check(result.first, 10);

    result = pointlike::verticalDistance(p2, seg);
    REQUIRE(result.second);
    check(result.first, -10);

    result = pointlike::verticalDistance(Point{10, 20}, seg);
    REQUIRE(result.second);
    check(result.first, 10);


    Point p4 = {80, 0};
    Segment seg2 = { {0, 0}, {0, 40} };

    result = pointlike::horizontalDistance(p4, seg2);

    REQUIRE(result.second);
    check(result.first, 80);

    result = pointlike::verticalDistance(p4, seg2);
    // Point should not be related to the segment
    REQUIRE_FALSE(result.second);

}

TEST_CASE("Area", "[Geometry]") {
    using namespace libnest2d;

    RectangleItem rect(10, 10);

    REQUIRE(rect.area() == Approx(100));

    RectangleItem rect2 = {100, 100};

    REQUIRE(rect2.area() == Approx(10000));

    Item item = {
        {61, 97},
        {70, 151},
        {176, 151},
        {189, 138},
        {189, 59},
        {70, 59},
        {61, 77},
        {61, 97}
    };

    REQUIRE(shapelike::area(item.transformedShape()) > 0 );
}

TEST_CASE("IsPointInsidePolygon", "[Geometry]") {
    using namespace libnest2d;

    RectangleItem rect(10, 10);

    Point p = {1, 1};

    REQUIRE(rect.isInside(p));

    p = {11, 11};

    REQUIRE_FALSE(rect.isInside(p));


    p = {11, 12};

    REQUIRE_FALSE(rect.isInside(p));


    p = {3, 3};

    REQUIRE(rect.isInside(p));

}

//TEST_CASE(GeometryAlgorithms, Intersections) {
//    using namespace binpack2d;

//    RectangleItem rect(70, 30);

//    rect.translate({80, 60});

//    RectangleItem rect2(80, 60);
//    rect2.translate({80, 0});

////    REQUIRE_FALSE(Item::intersects(rect, rect2));

//    Segment s1({0, 0}, {10, 10});
//    Segment s2({1, 1}, {11, 11});
//    REQUIRE_FALSE(ShapeLike::intersects(s1, s1));
//    REQUIRE_FALSE(ShapeLike::intersects(s1, s2));
//}

TEST_CASE("LeftAndDownPolygon", "[Geometry]")
{
    using namespace libnest2d;

    Box bin(100, 100);
    BottomLeftPlacer placer(bin);

    Item item = {{70, 75}, {88, 60}, {65, 50}, {60, 30}, {80, 20}, {42, 20},
                 {35, 35}, {35, 55}, {40, 75}, {70, 75}};

    Item leftControl = { {40, 75},
                        {35, 55},
                        {35, 35},
                        {42, 20},
                        {0,  20},
                        {0,  75},
                        {40, 75}};

    Item downControl = {{88, 60},
                        {88, 0},
                        {35, 0},
                        {35, 35},
                        {42, 20},
                        {80, 20},
                        {60, 30},
                        {65, 50},
                        {88, 60}};

    Item leftp(placer.leftPoly(item));

    REQUIRE(shapelike::isValid(leftp.rawShape()).first);
    REQUIRE(leftp.vertexCount() == leftControl.vertexCount());

    for(unsigned long i = 0; i < leftControl.vertexCount(); i++) {
        REQUIRE(getX(leftp.vertex(i)) == getX(leftControl.vertex(i)));
        REQUIRE(getY(leftp.vertex(i)) == getY(leftControl.vertex(i)));
    }

    Item downp(placer.downPoly(item));

    REQUIRE(shapelike::isValid(downp.rawShape()).first);
    REQUIRE(downp.vertexCount() == downControl.vertexCount());

    for(unsigned long i = 0; i < downControl.vertexCount(); i++) {
        REQUIRE(getX(downp.vertex(i)) == getX(downControl.vertex(i)));
        REQUIRE(getY(downp.vertex(i)) == getY(downControl.vertex(i)));
    }
}

TEST_CASE("ArrangeRectanglesTight", "[Nesting]")
{
    using namespace libnest2d;

    std::vector<RectangleItem> rects = {
        {80, 80},
        {60, 90},
        {70, 30},
        {80, 60},
        {60, 60},
        {60, 40},
        {40, 40},
        {10, 10},
        {10, 10},
        {10, 10},
        {10, 10},
        {10, 10},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {20, 20} };

    Box bin(210, 250, {105, 125});

    REQUIRE(bin.width() == 210);
    REQUIRE(bin.height() == 250);
    REQUIRE(getX(bin.center()) == 105);
    REQUIRE(getY(bin.center()) == 125);

    _Nester<BottomLeftPlacer, FirstFitSelection> arrange(bin);

    arrange.execute(rects.begin(), rects.end());

    auto max_group = std::max_element(rects.begin(), rects.end(),
                                      [](const Item &i1, const Item &i2) {
                                          return i1.binId() < i2.binId();
                                      });

    int groups = max_group == rects.end() ? 0 : max_group->binId() + 1;

    REQUIRE(groups == 1u);
    REQUIRE(
        std::all_of(rects.begin(), rects.end(), [](const RectangleItem &itm) {
            return itm.binId() != BIN_ID_UNSET;
        }));

    // check for no intersections, no containment:

    bool valid = true;
    for(Item& r1 : rects) {
        for(Item& r2 : rects) {
            if(&r1 != &r2 ) {
                valid = !Item::intersects(r1, r2) || Item::touches(r1, r2);
                REQUIRE(valid);
                valid = (valid && !r1.isInside(r2) && !r2.isInside(r1));
                REQUIRE(valid);
            }
        }
    }
}

TEST_CASE("ArrangeRectanglesLoose", "[Nesting]")
{
    using namespace libnest2d;

    //    std::vector<Rectangle> rects = { {40, 40}, {10, 10}, {20, 20} };
    std::vector<RectangleItem> rects = {
        {80, 80},
        {60, 90},
        {70, 30},
        {80, 60},
        {60, 60},
        {60, 40},
        {40, 40},
        {10, 10},
        {10, 10},
        {10, 10},
        {10, 10},
        {10, 10},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {20, 20} };

    Box bin(210, 250, {105, 125});

    REQUIRE(bin.width() == 210);
    REQUIRE(bin.height() == 250);
    REQUIRE(getX(bin.center()) == 105);
    REQUIRE(getY(bin.center()) == 125);

    Coord min_obj_distance = 5;

    _Nester<BottomLeftPlacer, FirstFitSelection> arrange(bin, min_obj_distance);

    arrange.execute(rects.begin(), rects.end());

    auto max_group = std::max_element(rects.begin(), rects.end(),
                                      [](const Item &i1, const Item &i2) {
                                          return i1.binId() < i2.binId();
                                      });

    auto groups = size_t(max_group == rects.end() ? 0 : max_group->binId() + 1);

    REQUIRE(groups == 1u);
    REQUIRE(
        std::all_of(rects.begin(), rects.end(), [](const RectangleItem &itm) {
            return itm.binId() != BIN_ID_UNSET;
        }));

    // check for no intersections, no containment:
    bool valid = true;
    for(Item& r1 : rects) {
        for(Item& r2 : rects) {
            if(&r1 != &r2 ) {
                valid = !Item::intersects(r1, r2);
                valid = (valid && !r1.isInside(r2) && !r2.isInside(r1));
                REQUIRE(valid);
            }
        }
    }

}

namespace {
using namespace libnest2d;

template<long long SCALE = 1, class Bin>
void exportSVG(std::vector<std::reference_wrapper<Item>>& result, const Bin& bin, int idx = 0) {
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
        Item rbin( RectangleItem(bin.width(), bin.height()) );
        for(unsigned j = 0; j < rbin.vertexCount(); j++) {
            auto v = rbin.vertex(j);
            setY(v, -getY(v)/SCALE + 500 );
            setX(v, getX(v)/SCALE);
            rbin.setVertex(j, v);
        }
        out << shapelike::serialize<Formats::SVG>(rbin.rawShape()) << std::endl;
        for(Item& sh : r) {
            Item tsh(sh.transformedShape());
            for(unsigned j = 0; j < tsh.vertexCount(); j++) {
                auto v = tsh.vertex(j);
                setY(v, -getY(v)/SCALE + 500);
                setX(v, getX(v)/SCALE);
                tsh.setVertex(j, v);
            }
            out << shapelike::serialize<Formats::SVG>(tsh.rawShape()) << std::endl;
        }
        out << "\n</svg>" << std::endl;
    }
    out.close();

    //        i++;
    //    }
}
}

TEST_CASE("BottomLeftStressTest", "[Geometry]") {
    using namespace libnest2d;

    const Coord SCALE = 1000000;
    auto& input = prusaParts();

    Box bin(210*SCALE, 250*SCALE);
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
                exportSVG(result, bin, i);
            }
            REQUIRE(valid);
        } else {
            std::cout << "something went terribly wrong!" << std::endl;
            FAIL();
        }

        placer.clearItems();
        it++;
        i++;
    }
}

TEST_CASE("convexHull", "[Geometry]") {
    using namespace libnest2d;

    ClipperLib::Path poly = PRINTER_PART_POLYGONS[0];

    auto chull = sl::convexHull(poly);

    REQUIRE(chull.size() == poly.size());
}

TEST_CASE("PrusaPartsShouldFitIntoTwoBins", "[Nesting]") {

    // Get the input items and define the bin.
    std::vector<Item> input = prusaParts();
    auto bin = Box(250000000, 210000000);

    // Do the nesting. Check in each step if the remaining items are less than
    // in the previous step. (Some algorithms can place more items in one step)
    size_t pcount = input.size();

    size_t bins = libnest2d::nest(input, bin, 0, {},
                                  ProgressFunction{[&pcount](unsigned cnt) {
                                      REQUIRE(cnt < pcount);
                                      pcount = cnt;
                                  }});

    // For prusa parts, 2 bins should be enough...
    REQUIRE(bins > 0u);
    REQUIRE(bins <= 2u);

    // All parts should be processed by the algorithm
    REQUIRE(
        std::all_of(input.begin(), input.end(), [](const Item &itm) {
            return itm.binId() != BIN_ID_UNSET;
        }));

    // Gather the items into piles of arranged polygons...
    using Pile = TMultiShape<ClipperLib::Polygon>;
    std::vector<Pile> piles(bins);

    for (auto &itm : input)
        piles[size_t(itm.binId())].emplace_back(itm.transformedShape());

    // Now check all the piles, the bounding box of each pile should be inside
    // the defined bin.
    for (auto &pile : piles) {
        auto bb = sl::boundingBox(pile);
        REQUIRE(sl::isInside(bb, bin));
    }
}

TEST_CASE("EmptyItemShouldBeUntouched", "[Nesting]") {
    auto bin = Box(250000000, 210000000); // dummy bin

    std::vector<Item> items;
    items.emplace_back(Item{});   // Emplace empty item
    items.emplace_back(Item{0, 200, 0});   // Emplace zero area item

    size_t bins = libnest2d::nest(items, bin);

    REQUIRE(bins == 0u);
    for (auto &itm : items) REQUIRE(itm.binId() == BIN_ID_UNSET);
}

TEST_CASE("LargeItemShouldBeUntouched", "[Nesting]") {
    auto bin = Box(250000000, 210000000); // dummy bin

    std::vector<Item> items;
    items.emplace_back(RectangleItem{250000001, 210000001});  // Emplace large item

    size_t bins = libnest2d::nest(items, bin);

    REQUIRE(bins == 0u);
    REQUIRE(items.front().binId() == BIN_ID_UNSET);
}

TEST_CASE("Items can be preloaded", "[Nesting]") {
    auto bin = Box({0, 0}, {250000000, 210000000}); // dummy bin

    std::vector<Item> items;
    items.reserve(2);

    NestConfig<> cfg;
    cfg.placer_config.alignment = NestConfig<>::Placement::Alignment::DONT_ALIGN;

    items.emplace_back(RectangleItem{10000000, 10000000});
    Item &fixed_rect = items.back();
    fixed_rect.translate(bin.center());

    items.emplace_back(RectangleItem{20000000, 20000000});
    Item &movable_rect = items.back();
    movable_rect.translate(bin.center());

    SECTION("Preloaded Item should be untouched") {
        fixed_rect.markAsFixedInBin(0);

        size_t bins = libnest2d::nest(items, bin, 0, cfg);

        REQUIRE(bins == 1);

        REQUIRE(fixed_rect.binId() == 0);
        REQUIRE(fixed_rect.translation().X == bin.center().X);
        REQUIRE(fixed_rect.translation().Y == bin.center().Y);

        REQUIRE(movable_rect.binId() == 0);
        REQUIRE(movable_rect.translation().X != bin.center().X);
        REQUIRE(movable_rect.translation().Y != bin.center().Y);
    }

    SECTION("Preloaded Item should not affect free bins") {
        fixed_rect.markAsFixedInBin(1);

        size_t bins = libnest2d::nest(items, bin, 0, cfg);

        REQUIRE(bins == 2);

        REQUIRE(fixed_rect.binId() == 1);
        REQUIRE(fixed_rect.translation().X == bin.center().X);
        REQUIRE(fixed_rect.translation().Y == bin.center().Y);

        REQUIRE(movable_rect.binId() == 0);

        auto bb = movable_rect.boundingBox();
        REQUIRE(bb.center().X == bin.center().X);
        REQUIRE(bb.center().Y == bin.center().Y);
    }
}

namespace {

struct ItemPair {
    Item orbiter;
    Item stationary;
};

std::vector<ItemPair> nfp_testdata = {
    {
        {
            {80, 50},
            {100, 70},
            {120, 50},
            {80, 50}
        },
        {
            {10, 10},
            {10, 40},
            {40, 40},
            {40, 10},
            {10, 10}
        }
    },
    {
        {
            {80, 50},
            {60, 70},
            {80, 90},
            {120, 90},
            {140, 70},
            {120, 50},
            {80, 50}
        },
        {
            {10, 10},
            {10, 40},
            {40, 40},
            {40, 10},
            {10, 10}
        }
    },
    {
        {
            {40, 10},
            {30, 10},
            {20, 20},
            {20, 30},
            {30, 40},
            {40, 40},
            {50, 30},
            {50, 20},
            {40, 10}
        },
        {
            {80, 0},
            {80, 30},
            {110, 30},
            {110, 0},
            {80, 0}
        }
    },
    {
        {
            {117, 107},
            {118, 109},
            {120, 112},
            {122, 113},
            {128, 113},
            {130, 112},
            {132, 109},
            {133, 107},
            {133, 103},
            {132, 101},
            {130, 98},
            {128, 97},
            {122, 97},
            {120, 98},
            {118, 101},
            {117, 103},
            {117, 107}
            },
        {
            {102, 116},
            {111, 126},
            {114, 126},
            {144, 106},
            {148, 100},
            {148, 85},
            {147, 84},
            {102, 84},
            {102, 116},
            }
    },
    {
        {
            {99, 122},
            {108, 140},
            {110, 142},
            {139, 142},
            {151, 122},
            {151, 102},
            {142, 70},
            {139, 68},
            {111, 68},
            {108, 70},
            {99, 102},
            {99, 122},
            },
        {
            {107, 124},
            {128, 125},
            {133, 125},
            {136, 124},
            {140, 121},
            {142, 119},
            {143, 116},
            {143, 109},
            {141, 93},
            {139, 89},
            {136, 86},
            {134, 85},
            {108, 85},
            {107, 86},
            {107, 124},
            }
    },
    {
        {
            {91, 100},
            {94, 144},
            {117, 153},
            {118, 153},
            {159, 112},
            {159, 110},
            {156, 66},
            {133, 57},
            {132, 57},
            {91, 98},
            {91, 100},
            },
        {
            {101, 90},
            {103, 98},
            {107, 113},
            {114, 125},
            {115, 126},
            {135, 126},
            {136, 125},
            {144, 114},
            {149, 90},
            {149, 89},
            {148, 87},
            {145, 84},
            {105, 84},
            {102, 87},
            {101, 89},
            {101, 90},
            }
    }
};

    std::vector<ItemPair> nfp_concave_testdata = {
        { // ItemPair
         {
             {
                 {533726, 142141},
                 {532359, 143386},
                 {530141, 142155},
                 {528649, 160091},
                 {533659, 157607},
                 {538669, 160091},
                 {537178, 142155},
                 {534959, 143386},
                 {533726, 142141},
                 }
             },
         {
             {
                 {118305, 11603},
                 {118311, 26616},
                 {113311, 26611},
                 {109311, 29604},
                 {109300, 44608},
                 {109311, 49631},
                 {113300, 52636},
                 {118311, 52636},
                 {118308, 103636},
                 {223830, 103636},
                 {236845, 90642},
                 {236832, 11630},
                 {232825, 11616},
                 {210149, 11616},
                 {211308, 13625},
                 {209315, 17080},
                 {205326, 17080},
                 {203334, 13629},
                 {204493, 11616},
                 {118305, 11603},
                 }
             },
         }
};

template<nfp::NfpLevel lvl, Coord SCALE>
void testNfp(const std::vector<ItemPair>& testdata) {
    using namespace libnest2d;

    Box bin(210*SCALE, 250*SCALE);

    int TEST_CASEcase = 0;

    auto& exportfun = exportSVG<SCALE, Box>;

    auto onetest = [&](Item& orbiter, Item& stationary, unsigned /*testidx*/){
        TEST_CASEcase++;

        orbiter.translate({210*SCALE, 0});

        auto&& nfp = nfp::noFitPolygon<lvl>(stationary.rawShape(),
                                            orbiter.transformedShape());

        placers::correctNfpPosition(nfp, stationary, orbiter);

        auto valid = shapelike::isValid(nfp.first);

        /*Item infp(nfp.first);
        if(!valid.first) {
            std::cout << "TEST_CASE instance: " << TEST_CASEidx << " "
                      << valid.second << std::endl;
            std::vector<std::reference_wrapper<Item>> inp = {std::ref(infp)};
            exportfun(inp, bin, TEST_CASEidx);
        }*/

        REQUIRE(valid.first);

        Item infp(nfp.first);

        int i = 0;
        auto rorbiter = orbiter.transformedShape();
        auto vo = nfp::referenceVertex(rorbiter);

        REQUIRE(stationary.isInside(infp));

        for(auto v : infp) {
            auto dx = getX(v) - getX(vo);
            auto dy = getY(v) - getY(vo);

            Item tmp = orbiter;

            tmp.translate({dx, dy});

            bool touching = Item::touches(tmp, stationary);

            if(!touching || !valid.first) {
                std::vector<std::reference_wrapper<Item>> inp = {
                    std::ref(stationary), std::ref(tmp), std::ref(infp)
                };

                exportfun(inp, bin, TEST_CASEcase*i++);
            }

            REQUIRE(touching);
        }
    };

    unsigned tidx = 0;
    for(auto& td : testdata) {
        auto orbiter = td.orbiter;
        auto stationary = td.stationary;
        onetest(orbiter, stationary, tidx++);
    }

    tidx = 0;
    for(auto& td : testdata) {
        auto orbiter = td.stationary;
        auto stationary = td.orbiter;
        onetest(orbiter, stationary, tidx++);
    }
}
}

TEST_CASE("nfpConvexConvex", "[Geometry]") {
    testNfp<nfp::NfpLevel::CONVEX_ONLY, 1>(nfp_testdata);
}

//TEST_CASE(GeometryAlgorithms, nfpConcaveConcave) {
//    TEST_CASENfp<NfpLevel::BOTH_CONCAVE, 1000>(nfp_concave_TEST_CASEdata);
//}

TEST_CASE("pointOnPolygonContour", "[Geometry]") {
    using namespace libnest2d;

    RectangleItem input(10, 10);

    placers::EdgeCache<PolygonImpl> ecache(input);

    auto first = *input.begin();
    REQUIRE(getX(first) == getX(ecache.coords(0)));
    REQUIRE(getY(first) == getY(ecache.coords(0)));

    auto last = *std::prev(input.end());
    REQUIRE(getX(last) == getX(ecache.coords(1.0)));
    REQUIRE(getY(last) == getY(ecache.coords(1.0)));

    for(int i = 0; i <= 100; i++) {
        auto v = ecache.coords(i*(0.01));
        REQUIRE(shapelike::touches(v, input.transformedShape()));
    }
}

TEST_CASE("mergePileWithPolygon", "[Geometry]") {
    using namespace libnest2d;

    RectangleItem rect1(10, 15);
    RectangleItem rect2(15, 15);
    RectangleItem rect3(20, 15);

    rect2.translate({10, 0});
    rect3.translate({25, 0});

    TMultiShape<PolygonImpl> pile;
    pile.push_back(rect1.transformedShape());
    pile.push_back(rect2.transformedShape());

    auto result = nfp::merge(pile, rect3.transformedShape());

    REQUIRE(result.size() == 1);

    RectangleItem ref(45, 15);

    REQUIRE(shapelike::area(result.front()) == Approx(ref.area()));
}

namespace {

long double refMinAreaBox(const PolygonImpl& p) {

    auto it = sl::cbegin(p), itx = std::next(it);

    long double min_area = std::numeric_limits<long double>::max();


    auto update_min = [&min_area, &it, &itx, &p]() {
        Segment s(*it, *itx);

        PolygonImpl rotated = p;
        sl::rotate(rotated, -s.angleToXaxis());
        auto bb = sl::boundingBox(rotated);
        auto area = cast<long double>(sl::area(bb));
        if(min_area > area) min_area = area;
    };

    while(itx != sl::cend(p)) {
        update_min();
        ++it; ++itx;
    }

    it = std::prev(sl::cend(p)); itx = sl::cbegin(p);
    update_min();

    return min_area;
}

template<class T> struct BoostGCD {
    T operator()(const T &a, const T &b) { return boost::gcd(a, b); }
};

using Unit = int64_t;
using Ratio = boost::rational<boost::multiprecision::int128_t>;

}

//TEST_CASE(GeometryAlgorithms, MinAreaBBCClk) {
//    auto u = [](ClipperLib::cInt n) { return n*1000000; };
//    PolygonImpl poly({ {u(0), u(0)}, {u(4), u(1)}, {u(2), u(4)}});

//    long double arearef = refMinAreaBox(poly);
//    long double area = minAreaBoundingBox<PolygonImpl, Unit, Ratio>(poly).area();

//    REQUIRE(std::abs(area - arearef) <= 500e6 );
//}

TEST_CASE("MinAreaBBWithRotatingCalipers", "[Geometry]") {
    long double err_epsilon = 500e6l;

    for(ClipperLib::Path rinput : PRINTER_PART_POLYGONS) {
        PolygonImpl poly(rinput);

        long double arearef = refMinAreaBox(poly);
        auto bb = minAreaBoundingBox<PathImpl, Unit, Ratio>(rinput);
        long double area = cast<long double>(bb.area());

        bool succ = std::abs(arearef - area) < err_epsilon;

        REQUIRE(succ);
    }

    for(ClipperLib::Path rinput : STEGOSAUR_POLYGONS) {
        rinput.pop_back();
        std::reverse(rinput.begin(), rinput.end());

        PolygonImpl poly(removeCollinearPoints<PathImpl, PointImpl, Unit>(rinput, 1000000));

        long double arearef = refMinAreaBox(poly);
        auto bb = minAreaBoundingBox<PolygonImpl, Unit, Ratio>(poly);
        long double area = cast<long double>(bb.area());


        bool succ = std::abs(arearef - area) < err_epsilon;

        REQUIRE(succ);
    }
}
