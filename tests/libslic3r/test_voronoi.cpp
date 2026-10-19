#include <algorithm>
#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include "test_utils.hpp"

#include "libslic3r/Geometry/Voronoi.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Line.hpp"
#include <cmath>
#include <cstddef>
#include "libslic3r/BoundingBox.hpp"
#include <libslic3r/Polygon.hpp>
#include <libslic3r/Polyline.hpp>
#include <libslic3r/EdgeGrid.hpp>
#include <libslic3r/Geometry.hpp>

#include <libslic3r/Geometry/VoronoiOffset.hpp>
#include <libslic3r/Geometry/VoronoiVisualUtils.hpp>

#include "libslic3r/libslic3r.h"
#include <math.h>
#include <numeric>
#include <vector>

// #define VORONOI_DEBUG_OUT

#ifdef VORONOI_DEBUG_OUT
#include <libslic3r/VoronoiVisualUtils.hpp>
#endif

using namespace Slic3r;

using VD = Geometry::VoronoiDiagram;
using BoostVD = boost::polygon::voronoi_diagram<double>;

struct OffsetTest {
    double distance;
    size_t num_outer;
    size_t num_inner;
};

TEST_CASE("Voronoi offset", "[VoronoiOffset]")
{
  Polygons poly_with_hole = { Polygon {
        {       0, 10000000},
        {  700000,        0},
        {  700000,  9000000},
        { 9100000,  9000000},
        { 9100000,        0},
        {10000000, 10000000}
        }
  };

  double area = std::accumulate(poly_with_hole.begin(), poly_with_hole.end(), 0., [](double a, auto &poly){ return a + poly.area(); });
  REQUIRE(area > 0.);

  VD vd;
  Lines lines = to_lines(poly_with_hole);
  vd.construct_voronoi(lines.begin(), lines.end());

  for (const OffsetTest &ot : {
            OffsetTest { scale_(0.2), 1, 1 },
            OffsetTest { scale_(0.4), 1, 1 },
            OffsetTest { scale_(0.5), 1, 2 },
            OffsetTest { scale_(0.505), 1, 2 },
            OffsetTest { scale_(0.51), 1, 2 },
            OffsetTest { scale_(0.52), 1, 1 },
            OffsetTest { scale_(0.53), 1, 1 },
            OffsetTest { scale_(0.54), 1, 1 },
            OffsetTest { scale_(0.55), 1, 0 }
      }) {

      Polygons offsetted_polygons_in = Slic3r::Voronoi::offset(vd, lines, - ot.distance, scale_(0.005));
#ifdef VORONOI_DEBUG_OUT
      dump_voronoi_to_svg(debug_out_path("voronoi-offset-in-%lf.svg", ot.distance).c_str(),
          vd, Points(), lines, offsetted_polygons_in);
#endif
      REQUIRE(offsetted_polygons_in.size() == ot.num_inner);
  }
}

TEST_CASE("Voronoi offset 2", "[VoronoiOffset]")
{
    coord_t mm = coord_t(scale_(1.));
    Polygons poly = {
        Polygon {
            { 0, 0 },
            { 1, 0 },
            { 1, 1 },
            { 2, 1 },
            { 2, 0 },
            { 3, 0 },
            { 3, 2 },
            { 0, 2 }
        },
        Polygon {
            { 0, - 1 - 2 },
            { 3, - 1 - 2 },
            { 3, - 1 - 0 },
            { 2, - 1 - 0 },
            { 2, - 1 - 1 },
            { 1, - 1 - 1 },
            { 1, - 1 - 0 },
            { 0, - 1 - 0 }
        },
    };
    for (Polygon &p : poly)
        for (Point &pt : p.points)
            pt *= mm;

  double area = std::accumulate(poly.begin(), poly.end(), 0., [](double a, auto &poly){ return a + poly.area(); });
  REQUIRE(area > 0.);

  VD vd;
  Lines lines = to_lines(poly);
  vd.construct_voronoi(lines.begin(), lines.end());

  for (const OffsetTest &ot : {
            OffsetTest { scale_(0.2), 2, 2 },
            OffsetTest { scale_(0.4), 2, 2 },
            OffsetTest { scale_(0.45), 2, 2 },
            OffsetTest { scale_(0.48), 2, 2 },
            OffsetTest { scale_(0.5), 2, 4 },
            OffsetTest { scale_(0.505), 2, 4 },
            OffsetTest { scale_(0.7), 2, 0 },
            OffsetTest { scale_(0.8), 1, 0 }
      }) {

      Polygons offsetted_polygons_out = Slic3r::Voronoi::offset(vd, lines, ot.distance, scale_(0.005));
#ifdef VORONOI_DEBUG_OUT
      dump_voronoi_to_svg(debug_out_path("voronoi-offset2-out-%lf.svg", ot.distance).c_str(),
          vd, Points(), lines, offsetted_polygons_out);
#endif
      REQUIRE(offsetted_polygons_out.size() == ot.num_outer);

      Polygons offsetted_polygons_in = Slic3r::Voronoi::offset(vd, lines, - ot.distance, scale_(0.005));
#ifdef VORONOI_DEBUG_OUT
      dump_voronoi_to_svg(debug_out_path("voronoi-offset2-in-%lf.svg", ot.distance).c_str(),
          vd, Points(), lines, offsetted_polygons_in);
#endif
      REQUIRE(offsetted_polygons_in.size() == ot.num_inner);
  }
}

TEST_CASE("Voronoi offset 3", "[VoronoiOffset]")
{
    coord_t mm = coord_t(scale_(1.));
    Polygons poly = {
        Polygon {
            { 0, 0 },
            { 2, 0 },
            { 2, 1 },
            { 3, 1 },
            { 3, 0 },
            { 5, 0 },
            { 5, 2 },
            { 4, 2 },
            { 4, 3 },
            { 1, 3 },
            { 1, 2 },
            { 0, 2 }
        },
        Polygon {
            { 0, -1 - 2 },
            { 1, -1 - 2 },
            { 1, -1 - 3 },
            { 4, -1 - 3 },
            { 4, -1 - 2 },
            { 5, -1 - 2 },
            { 5, -1 - 0 },
            { 3, -1 - 0 },
            { 3, -1 - 1 },
            { 2, -1 - 1 },
            { 2, -1 - 0 },
            { 0, -1 - 0 }
        },
    };
    for (Polygon &p : poly) {
        REQUIRE(p.area() > 0.);
        for (Point &pt : p.points)
            pt *= mm;
    }

  VD vd;
  Lines lines = to_lines(poly);
  vd.construct_voronoi(lines.begin(), lines.end());

  for (const OffsetTest &ot : {
            OffsetTest { scale_(0.2), 2, 2 },
            OffsetTest { scale_(0.4), 2, 2 },
            OffsetTest { scale_(0.49), 2, 2 },
            OffsetTest { scale_(0.5), 2, 2 },
            OffsetTest { scale_(0.51), 2, 2 },
            OffsetTest { scale_(0.56), 2, 2 },
            OffsetTest { scale_(0.6), 2, 2 },
            OffsetTest { scale_(0.7), 2, 2 },
            OffsetTest { scale_(0.8), 1, 6 },
            OffsetTest { scale_(0.9), 1, 6 },
            OffsetTest { scale_(0.99), 1, 6 },
            OffsetTest { scale_(1.0), 1, 0 },
            OffsetTest { scale_(1.01), 1, 0 },
      }) {

      Polygons offsetted_polygons_out = Slic3r::Voronoi::offset(vd, lines, ot.distance, scale_(0.005));
#ifdef VORONOI_DEBUG_OUT
      dump_voronoi_to_svg(debug_out_path("voronoi-offset3-out-%lf.svg", ot.distance).c_str(),
          vd, Points(), lines, offsetted_polygons_out);
#endif
      REQUIRE(offsetted_polygons_out.size() == ot.num_outer);

      Polygons offsetted_polygons_in = Slic3r::Voronoi::offset(vd, lines, - ot.distance, scale_(0.005));
#ifdef VORONOI_DEBUG_OUT
      dump_voronoi_to_svg(debug_out_path("voronoi-offset3-in-%lf.svg", ot.distance).c_str(),
          vd, Points(), lines, offsetted_polygons_in);
#endif
      REQUIRE(offsetted_polygons_in.size() == ot.num_inner);
  }
}

TEST_CASE("Voronoi offset with edge collapse", "[VoronoiOffset4]")
{
    Polygons poly 
    {
        // Outer contour
        {
            { 434890, -64754575 },
            { 541060, -64669076 },
            { 585647, -64583538 },
            { 576999, -64484233 },
            { 485666, -64191173 },
            { 1029323, -63646261 },
            { 1416925, -63666243 },
            { 1541532, -63643075 },
            { 1541535, -63643075 },
            { 1541535, -63643074 },
            { 1552612, -63641015 },
            { 1631609, -63568270 },
            { 1678393, -63443639 },
            { 1725519, -63219942 },
            { 1770552, -62903321 },
            { 1742544, -62755934 },
            { 1758152, -62588378 },
            { 1702289, -62474826 },
            { 1638627, -62242058 },
            { 1671281, -61938568 },
            { 1792676, -61478332 },
            { 1934894, -60760597 },
            { 2160333, -60064089 },
            { 2638183, -58972890 },
            { 2933095, -58478536 },
            { 3173586, -58159527 },
            { 3557779, -57556727 },
            { 3707088, -57158174 },
            { 3758053, -56907139 },
            { 3935932, -56278649 },
            { 4077331, -56033426 },
            { 4151386, -55768739 },
            { 4293796, -55561683 },
            { 4383089, -55387391 },
            { 4614128, -55201443 },
            { 5268755, -54333831 },
            { 5547634, -53797296 },
            { 5573864, -53639250 },
            { 5736812, -53304157 },
            { 6090973, -52066302 },
            { 6148630, -51666664 },
            { 6189511, -50974991 },
            { 6244112, -50774489 },
            { 6302489, -50761556 },
            { 6511179, -50579861 },
            { 6778128, -50398667 },
            { 6896879, -50350264 },
            { 7388259, -49913262 },
            { 7630584, -49602449 },
            { 7886846, -48987881 },
            { 7871333, -48318666 },
            { 7770349, -47841179 },
            { 7570223, -47234733 },
            { 7283115, -46705503 },
            { 6842948, -46056539 },
            { 6612732, -45760004 },
            { 6150430, -44991494 },
            { 5564326, -44168114 },
            { 5193032, -43807544 },
            { 4932097, -43489047 },
            { 3857385, -42548846 },
            { 3764745, -42081468 },
            { 3539887, -41422273 },
            { 3283048, -40803856 },
            { 3005925, -40367981 },
            { 3136185, -39834533 },
            { 3333757, -38499972 },
            { 3360660, -38183895 },
            { 3353375, -38036005 },
            { 3398808, -37582504 },
            { 3604229, -37221781 },
            { 3698079, -36962934 },
            { 4000449, -36007804 },
            { 4256811, -35016707 },
            { 4405951, -34581428 },
            { 4364534, -34178439 },
            { 4124603, -33432250 },
            { 3806159, -32765167 },
            { 3359088, -32109020 },
            { 2880790, -31317192 },
            { 1548334, -30402139 },
            { -7, -30131023 },
            { -1548347, -30402139 },
            { -2880803, -31317189 },
            { -3359100, -32109018 },
            { -3806173, -32765166 },
            { -4124617, -33432248 },
            { -4364548, -34178436 },
            { -4405966, -34581423 },
            { -4256825, -35016707 },
            { -4000461, -36007800 },
            { -3698093, -36962933 },
            { -3604243, -37221781 },
            { -3398823, -37582501 },
            { -3353390, -38036003 },
            { -3360675, -38183892 },
            { -3333772, -38499972 },
            { -3136200, -39834530 },
            { -3005940, -40367980 },
            { -3283062, -40803852 },
            { -3539902, -41422273 },
            { -3764761, -42081468 },
            { -3857402, -42548846 },
            { -4932112, -43489047 },
            { -5193047, -43807544 },
            { -5564341, -44168114 },
            { -6150446, -44991494 },
            { -6612748, -45760000 },
            { -6842965, -46056536 },
            { -7283130, -46705503 },
            { -7570238, -47234733 },
            { -7770365, -47841179 },
            { -7871349, -48318666 },
            { -7886860, -48987873 },
            { -7630599, -49602451 },
            { -7388277, -49913260 },
            { -6896896, -50350264 },
            { -6778145, -50398667 },
            { -6511196, -50579862 },
            { -6302504, -50761557 },
            { -6244127, -50774488 },
            { -6189527, -50974989 },
            { -6148648, -51666664 },
            { -6090990, -52066302 },
            { -5736829, -53304157 },
            { -5573882, -53639250 },
            { -5547651, -53797296 },
            { -5268772, -54333831 },
            { -4614145, -55201443 },
            { -4383106, -55387389 },
            { -4293814, -55561683 },
            { -4151404, -55768739 },
            { -4077350, -56033423 },
            { -3935952, -56278647 },
            { -3758072, -56907137 },
            { -3707107, -57158170 },
            { -3557796, -57556727 },
            { -3173605, -58159527 },
            { -2933113, -58478536 },
            { -2638201, -58972890 },
            { -2295003, -59738435 },
            { -2160353, -60064089 },
            { -1978487, -60596626 },
            { -1792695, -61478332 },
            { -1671298, -61938574 },
            { -1638646, -62242058 },
            { -1702306, -62474826 },
            { -1758168, -62588380 },
            { -1742563, -62755934 },
            { -1770570, -62903317 },
            { -1725537, -63219946 },
            { -1678412, -63443639 },
            { -1631627, -63568270 },
            { -1553479, -63640201 },
            { -1416944, -63666243 },
            { -998854, -63645714 },
            { -485685, -64191173 },
            { -577019, -64484225 },
            { -585667, -64583538 },
            { -541079, -64669076 },
            { -434910, -64754575 },
            { -294192, -64810609 },
            { 294172, -64810609 },
        },
        // Hole 1
        {
            { -842246, -45167428 },
            { -1473641, -45154392 },
            { -2181728, -45100161 },
            { -2804308, -44985842 },
            { -3100514, -44879269 },
            { -3836807, -44802155 },
            { -4035816, -44718913 },
            { -4167175, -44583299 },
            { -4496705, -44467674 },
            { -4486674, -44382045 },
            { -4343949, -44070577 },
            { -3740991, -43494686 },
            { -2701372, -43313358 },
            { -1493599, -43312838 },
            { -8, -43352868 },
            { 1414575, -43313336 },
            { 2701358, -43313358 },
            { 3095462, -43374735 },
            { 3740975, -43494686 },
            { 4343934, -44070583 },
            { 4486657, -44382044 },
            { 4496688, -44467670 },
            { 4167159, -44583300 },
            { 4035800, -44718913 },
            { 3836792, -44802155 },
            { 3100498, -44879269 },
            { 2804292, -44985842 },
            { 2181712, -45100161 },
            { 1473625, -45154389 },
            { 842231, -45167428 },
            { -8, -45232686 },
        },
        // Hole 2
        {
            { 1657455, -63016679 },
            { 1723196, -63056286 },
            { 1541535, -63643074 },
            { 1541532, -63643075 },
            { 1030020, -63645562 },
        }
    };


  VD vd;
  Lines lines = to_lines(poly);
  vd.construct_voronoi(lines.begin(), lines.end());

  for (const OffsetTest &ot : {
            OffsetTest { scale_(0.2), 2, 2 },
            OffsetTest { scale_(0.4), 2, 2 },
            OffsetTest { scale_(0.49), 2, 3 },
            OffsetTest { scale_(0.51), 2, 2 },
            OffsetTest { scale_(0.56), 2, 2 },
            OffsetTest { scale_(0.6), 2, 2 },
            OffsetTest { scale_(0.7), 2, 2 },
            OffsetTest { scale_(0.8), 2, 2 },
            OffsetTest { scale_(0.9), 2, 2 },
            OffsetTest { scale_(0.99), 1, 2 },
            OffsetTest { scale_(1.0), 1, 2 },
            OffsetTest { scale_(1.01), 1, 2 },
      }) {

      Polygons offsetted_polygons_out = Slic3r::Voronoi::offset(vd, lines, ot.distance, scale_(0.005));
#ifdef VORONOI_DEBUG_OUT
      dump_voronoi_to_svg(debug_out_path("voronoi-offset3-out-%lf.svg", ot.distance).c_str(),
          vd, Points(), lines, offsetted_polygons_out);
#endif
      REQUIRE(offsetted_polygons_out.size() == ot.num_outer);

      Polygons offsetted_polygons_in = Slic3r::Voronoi::offset(vd, lines, - ot.distance, scale_(0.005));
#ifdef VORONOI_DEBUG_OUT
      dump_voronoi_to_svg(debug_out_path("voronoi-offset3-in-%lf.svg", ot.distance).c_str(),
          vd, Points(), lines, offsetted_polygons_in);
#endif
      REQUIRE(offsetted_polygons_in.size() == ot.num_inner);
  }
}

// A sample extracted from file medallion_printable_fixed-teeth.stl from https://www.thingiverse.com/thing:1347129
// This test for offset scale_(2.9) and bigger
// triggers assert(r < std::max(d0, d1) + EPSILON) in function first_circle_segment_intersection_parameter.
TEST_CASE("Voronoi offset 5", "[VoronoiOffset5]")
{
    Polygons poly = {
        Polygon {
            {        0 , -16077501 },
            {  3015222 , -16142836 },
            {  3072642 , -16138163 },
            {  3094279 , -16105662 },
            {  3110660 , -15140728 },
            {  3157438 , -14105326 },
            {  3338367 , -11081394 },
            {  3443412 ,  -8381621 },
            {  3472489 ,  -6084497 },
            {  3445790 ,  -5962924 },
            {  3061725 ,  -6003484 },
            {  3030326 ,  -6030622 },
            {  2989343 ,  -6270378 },
            {  2903752 ,  -7368176 },
            {  2856704 ,  -7740619 },
            {  2795743 ,  -7978809 },
            {  2729231 ,  -8098866 },
            {  2666509 ,  -8131138 },
            {  2614169 ,  -8112308 },
            {  2561157 ,  -8032014 },
            {  2488290 ,  -7479351 },
            {  2453360 ,  -6911556 },
            {  2456148 ,  -6463146 },
            {  2546029 ,  -4580396 },
            {  2688181 ,  -2593262 },
            {  2717617 ,  -1700519 },
            {  2682232 ,  -1128411 },
            {  2631029 ,   -832886 },
            {  2535941 ,   -504483 },
            {  2399115 ,   -199303 },
            {  2290997 ,   -171213 },
            {   611824 ,   -132865 },
            {     6419 ,   -375849 },
            {  -611825 ,   -132865 },
            { -2377355 ,   -185241 },
            { -2420740 ,   -231171 },
            { -2535942 ,   -504484 },
            { -2631030 ,   -832887 },
            { -2684831 ,  -1150821 },
            { -2714840 ,  -1586454 },
            { -2688181 ,  -2593262 },
            { -2546030 ,  -4580396 },
            { -2456149 ,  -6463145 },
            { -2453361 ,  -6911557 },
            { -2488291 ,  -7479352 },
            { -2561159 ,  -8032018 },
            { -2614171 ,  -8112309 },
            { -2666509 ,  -8131138 },
            { -2729233 ,  -8098868 },
            { -2795744 ,  -7978809 },
            { -2856706 ,  -7740619 },
            { -2903752 ,  -7368176 },
            { -2989345 ,  -6270378 },
            { -3030327 ,  -6030622 },
            { -3061726 ,  -6003484 },
            { -3445790 ,  -5962924 },
            { -3472490 ,  -6084498 },
            { -3468804 ,  -7244095 },
            { -3399287 ,  -9714025 },
            { -3338368 , -11081395 },
            { -3141015 , -14446051 },
            { -3094280 , -16105662 },
            { -3072643 , -16138163 },
            { -3008836 , -16143225 }
        }
    };
    double area = std::accumulate(poly.begin(), poly.end(), 0., [](double a, auto &poly){ return a + poly.area(); });
    REQUIRE(area > 0.);

    VD vd;
    Lines lines = to_lines(poly);
    vd.construct_voronoi(lines.begin(), lines.end());

    for (const OffsetTest &ot : {
            OffsetTest { scale_(2.8), 1, 1 },
            OffsetTest { scale_(2.9), 1, 1 },
            OffsetTest { scale_(3.0), 1, 1 },
    }) {

        Polygons offsetted_polygons_out = Slic3r::Voronoi::offset(vd, lines, ot.distance, scale_(0.005));
#ifdef VORONOI_DEBUG_OUT
        dump_voronoi_to_svg(debug_out_path("voronoi-offset5-out-%lf.svg", ot.distance).c_str(),
            vd, Points(), lines, offsetted_polygons_out);
#endif
        REQUIRE(offsetted_polygons_out.size() == ot.num_outer);

        Polygons offsetted_polygons_in = Slic3r::Voronoi::offset(vd, lines, - ot.distance, scale_(0.005));
#ifdef VORONOI_DEBUG_OUT
        dump_voronoi_to_svg(debug_out_path("voronoi-offset5-in-%lf.svg", ot.distance).c_str(),
            vd, Points(), lines, offsetted_polygons_in);
#endif
        REQUIRE(offsetted_polygons_in.size() == ot.num_inner);
    }
}

TEST_CASE("Voronoi skeleton", "[VoronoiSkeleton]")
{
    coord_t mm = coord_t(scale_(1.));
    Polygons poly = {
        Polygon {
            { 0, 0 },
            { 1, 0 },
            { 1, 1 },
            { 2, 1 },
            { 2, 0 },
            { 3, 0 },
            { 3, 2 },
            { 0, 2 }
        },
        Polygon {
            { 0, - 1 - 2 },
            { 3, - 1 - 2 },
            { 3, - 1 - 0 },
            { 2, - 1 - 0 },
            { 2, - 1 - 1 },
            { 1, - 1 - 1 },
            { 1, - 1 - 0 },
            { 0, - 1 - 0 }
        },
    };
    for (Polygon &p : poly)
        for (Point &pt : p.points)
            pt *= mm;

    double area = std::accumulate(poly.begin(), poly.end(), 0., [](double a, auto &poly){ return a + poly.area(); });
    REQUIRE(area > 0.);

    VD vd;
    Lines lines = to_lines(poly);
    vd.construct_voronoi(lines.begin(), lines.end());
    Slic3r::Voronoi::annotate_inside_outside(vd, lines);
    Slic3r::Voronoi::annotate_inside_outside(vd, lines);
    static constexpr double threshold_alpha = M_PI / 12.; // 30 degrees
    std::vector<Vec2d> skeleton_edges = Slic3r::Voronoi::skeleton_edges_rough(vd, lines, threshold_alpha);

    REQUIRE(! skeleton_edges.empty());
}

// Simple detection with complexity N^2 if there is any point in the input polygons that doesn't have Voronoi vertex.
[[maybe_unused]] static bool has_missing_voronoi_vertices(const Polygons &polygons, const VD &vd)
{
    auto are_equal = [](const VD::vertex_type v, const Point &p) { return (Vec2d(v.x(), v.y()) - p.cast<double>()).norm() <= SCALED_EPSILON; };

    Points            poly_points = to_points(polygons);
    std::vector<bool> found_vertices(poly_points.size());
    for (const Point &point : poly_points)
        for (const auto &vertex : vd.vertices())
            if (are_equal(vertex, point)) {
                found_vertices[&point - &poly_points.front()] = true;
                break;
            }

    return std::find(found_vertices.begin(), found_vertices.end(), false) != found_vertices.end();
}

// This case is composed of one square polygon, and one of the edges is divided into two parts by a point that lies on this edge.
// In some applications, this point is unnecessary and can be removed (merge two parts to one edge). But for the case of
// multi-material segmentation, these points are necessary. In this case, Voronoi vertex for the point, which divides the edge
// into two parts. Even we add more points to the edge, and then for these points, the Voronoin vertex is also missing. An
// infinity-edge passes through the missing Voronoi vertex. Therefore, this missing Voronoi vertex and edge can be reconstructed
// using the intersection between the infinity-edge with the input polygon.
// Rotation of the polygon solves this problem.
TEST_CASE("Voronoi missing vertex 1", "[VoronoiMissingVertex1]")
{
    Polygon poly = {
        { 25000000,  25000000},
        {-25000000,  25000000},
        {-25000000, -25000000},
        {-12412500, -25000000},
//        {- 1650000, -25000000},
        { 25000000, -25000000}
    };

//    poly.rotate(PI / 6);

    REQUIRE(poly.area() > 0.);
    REQUIRE(intersecting_edges({poly}).empty());

    VD    vd;
    Lines lines = to_lines(poly);
    vd.construct_voronoi(lines.begin(), lines.end());
#ifdef VORONOI_DEBUG_OUT
    dump_voronoi_to_svg(debug_out_path("voronoi-missing-vertex1-out.svg").c_str(), vd, Points(), lines);
#endif

//    REQUIRE(!has_missing_voronoi_vertices({poly}, vd));
}

// This case is composed of two square polygons (contour and hole), and again one of the edges is divided into two parts by a
// point that lies on this edge, and for this point is Voronoi vertex missing. A difference between the previous and this case is
// that for this case, through the missing Voronoi vertex is passing a finite edge between two internal Voronoin vertices.
// Therefore, this missing Voronoi vertex and edge can be reconstructed using the intersection between the finite edge with the
// input polygon.
// Rotation of the polygons solves this problem.
TEST_CASE("Voronoi missing vertex 2", "[VoronoiMissingVertex2]")
{
    Polygons poly = {
        Polygon {
            { 50000000,  50000000},
            {-50000000,  50000000},
            {-50000000, -50000000},
            { 50000000, -50000000},
        },
        Polygon {
            {-45000000, -45000000},
            {-45000000,  45000000},
            { 45000000,  45000000},
            { 45000000,   8280000},
            { 45000000, -45000000},
        }
    };

//    polygons_rotate(poly, PI / 6);

    double area = std::accumulate(poly.begin(), poly.end(), 0., [](double a, auto &poly) { return a + poly.area(); });
    REQUIRE(area > 0.);
    REQUIRE(intersecting_edges(poly).empty());

    VD    vd;
    Lines lines = to_lines(poly);
    vd.construct_voronoi(lines.begin(), lines.end());
#ifdef VORONOI_DEBUG_OUT
    dump_voronoi_to_svg(debug_out_path("voronoi-missing-vertex2-out.svg").c_str(), vd, Points(), lines);
#endif

//    REQUIRE(!has_missing_voronoi_vertices(poly, vd));
}

// This case is composed of two polygons, and again one of the edges is divided into two parts by a point that lies on this edge,
// and for this point is Voronoi vertex missing. A difference between the previous cases and this case through the missing
// Voronoi vertex is passing finite edge between one inner Voronoi vertex and one outer Voronoi vertex.
// Rotating the polygon also help solve this problem.
TEST_CASE("Voronoi missing vertex 3", "[VoronoiMissingVertex3]")
{
    Polygons poly = {
        Polygon	{
            {-29715088, -29310899},
            {-29022573, -28618384},
            {-27771147, -27366958},
            {-28539221, -26519393},
            {-30619013, -28586348},
            {-29812018, -29407830},
        },
        Polygon	{
            {-27035112, -28071875},
            {-27367482, -27770679},
            {-28387008, -28790205},
            {-29309438, -29712635},
            {-29406319, -29809515},
            {-29032985, -30179156},
        }
    };
    double area = std::accumulate(poly.begin(), poly.end(), 0., [](double a, auto &poly){ return a + poly.area(); });
    REQUIRE(area > 0.);
    REQUIRE(intersecting_edges(poly).empty());

    //    polygons_rotate(poly, PI/180);
    //    polygons_rotate(poly, PI/6);

    BoostVD vd;
    Lines lines = to_lines(poly);
    construct_voronoi(lines.begin(), lines.end(), &vd);
#ifdef VORONOI_DEBUG_OUT
    dump_voronoi_to_svg(debug_out_path("voronoi-missing-vertex3-out.svg").c_str(), vd, Points(), lines);
#endif

//    REQUIRE(!has_missing_voronoi_vertices(poly, vd));
}

// In this case, the Voronoi vertex (146873, -146873) is included twice.
// Also, near to those duplicate Voronoi vertices is another Voronoi vertex (146872, -146872).
// Rotating the polygon will help solve this problem, but then there arise three very close Voronoi vertices.
// Rotating of the input polygon will help solve this problem.
TEST_CASE("Duplicate Voronoi vertices", "[Voronoi]")
{
    Polygon poly = {
        { 25000000,  25000000},
        {-25000000,  25000000},
        {-25000000, -25000000},
        {   146872, -25000000},
        {  9912498, -25000000},
        { 25000000, -25000000},
        { 25000000, - 8056252},
        { 25000000, -  146873},
        { 25000000,  10790627},
    };

//    poly.rotate(PI / 6);

    REQUIRE(poly.area() > 0.);
    REQUIRE(intersecting_edges({poly}).empty());

    VD    vd;
    Lines lines = to_lines(poly);
    vd.construct_voronoi(lines.begin(), lines.end());
#ifdef VORONOI_DEBUG_OUT
    dump_voronoi_to_svg(debug_out_path("voronoi-duplicate-vertices-out.svg").c_str(), vd, Points(), lines);
#endif

    [[maybe_unused]] auto has_duplicate_vertices = [](const VD &vd) -> bool {
        std::vector<Vec2d> vertices;
        for (const auto &vertex : vd.vertices())
            vertices.emplace_back(Vec2d(vertex.x(), vertex.y()));

        std::sort(vertices.begin(), vertices.end(), [](const Vec2d &l, const Vec2d &r) { return l.x() < r.x() || (l.x() == r.x() && l.y() < r.y()); });
        return std::unique(vertices.begin(), vertices.end()) != vertices.end();
    };

//    REQUIRE(!has_duplicate_vertices(vd));
}

// In this case, there are three very close Voronoi vertices like in the previous test case after rotation. There is also one
// missing Voronoi vertex. One infinity-edge (after clip [(146872, -70146871), (146872, -146871)]) passes through this missing
// Voronoi vertex. This infinite edge [(146872, -70146871), (146872, -146871)] and edge [(146873, -146873), (0, 0)] are intersecting.
// They intersect probably because the three points are very close to each other, with a combination of the missing Voronoi vertex.
// Rotating of the input polygon will help solve this problem.
TEST_CASE("Intersecting Voronoi edges", "[Voronoi]")
{
    Polygon poly = {
        { 25000000,  25000000},
        {-25000000,  25000000},
        {-25000000, -25000000},
        {   146872, -25000000},
        { 25000000, -25000000},
        { 25000000, -  146873},
    };

//    poly.rotate(PI / 6);

    REQUIRE(poly.area() > 0.);
    REQUIRE(intersecting_edges({poly}).empty());

    VD    vd;
    Lines lines = to_lines(poly);
    vd.construct_voronoi(lines.begin(), lines.end());
#ifdef VORONOI_DEBUG_OUT
    dump_voronoi_to_svg(debug_out_path("voronoi-intersecting-edges-out.svg").c_str(), vd, Points(), lines);
#endif

    [[maybe_unused]] auto has_intersecting_edges = [](const Polygon &poly, const VD &vd) -> bool {
        BoundingBox  bbox         = get_extents(poly);
        const double bbox_dim_max = double(std::max(bbox.size().x(), bbox.size().y()));

        std::vector<Voronoi::Internal::segment_type> segments;
        for (const Line &line : to_lines(poly))
            segments.emplace_back(Voronoi::Internal::point_type(double(line.a.x()), double(line.a.y())),
                                  Voronoi::Internal::point_type(double(line.b.x()), double(line.b.y())));

        Lines edges;
        for (const auto &edge : vd.edges())
            if (edge.cell()->source_index() < edge.twin()->cell()->source_index()) {
                if (edge.is_finite()) {
                    edges.emplace_back(Point(coord_t(edge.vertex0()->x()), coord_t(edge.vertex0()->y())),
                                       Point(coord_t(edge.vertex1()->x()), coord_t(edge.vertex1()->y())));
                } else if (edge.is_infinite()) {
                    std::vector<Voronoi::Internal::point_type> samples;
                    Voronoi::Internal::clip_infinite_edge(poly.points, segments, edge, bbox_dim_max, &samples);
                    if (!samples.empty())
                        edges.emplace_back(Point(coord_t(samples[0].x()), coord_t(samples[0].y())), Point(coord_t(samples[1].x()), coord_t(samples[1].y())));
                }
            }

        Point intersect_point;
        for (auto first_it = edges.begin(); first_it != edges.end(); ++first_it)
            for (auto second_it = first_it + 1; second_it != edges.end(); ++second_it)
                if (first_it->intersection(*second_it, &intersect_point) && first_it->a != intersect_point && first_it->b != intersect_point)
                    return true;
        return false;
    };

//    REQUIRE(!has_intersecting_edges(poly, vd));
}
