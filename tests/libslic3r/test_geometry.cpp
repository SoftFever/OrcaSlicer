#include <catch2/catch.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ShortestPath.hpp"

//#include <random>
//#include "libnest2d/tools/benchmark.h"
#include "libslic3r/SVG.hpp"

#include "../libnest2d/printer_parts.hpp"

#include <unordered_set>

using namespace Slic3r;

TEST_CASE("Polygon::contains works properly", "[Geometry]"){
   // this test was failing on Windows (GH #1950)
    Slic3r::Polygon polygon(std::vector<Point>({
        Point(207802834,-57084522),
        Point(196528149,-37556190),
        Point(173626821,-25420928),
        Point(171285751,-21366123),
        Point(118673592,-21366123),
        Point(116332562,-25420928),
        Point(93431208,-37556191),
        Point(82156517,-57084523),
        Point(129714478,-84542120),
        Point(160244873,-84542120)
    }));
    Point point(95706562, -57294774);
    REQUIRE(polygon.contains(point));
}

SCENARIO("Intersections of line segments", "[Geometry]"){
    GIVEN("Integer coordinates"){
        Line line1(Point(5,15),Point(30,15));
        Line line2(Point(10,20), Point(10,10));
        THEN("The intersection is valid"){
            Point point;
            line1.intersection(line2,&point);
            REQUIRE(Point(10,15) == point);
        }
    }

    GIVEN("Scaled coordinates"){
        Line line1(Point(73.6310778185108 / 0.00001, 371.74239268924 / 0.00001), Point(73.6310778185108 / 0.00001, 501.74239268924 / 0.00001));
        Line line2(Point(75/0.00001, 437.9853/0.00001), Point(62.7484/0.00001, 440.4223/0.00001));
        THEN("There is still an intersection"){
            Point point;
            REQUIRE(line1.intersection(line2,&point));
        }
    }
}

/*
Tests for unused methods still written in perl
{
    my $polygon = Slic3r::Polygon->new(
        [45919000, 515273900], [14726100, 461246400], [14726100, 348753500], [33988700, 315389800], 
        [43749700, 343843000], [45422300, 352251500], [52362100, 362637800], [62748400, 369577600], 
        [75000000, 372014700], [87251500, 369577600], [97637800, 362637800], [104577600, 352251500], 
        [107014700, 340000000], [104577600, 327748400], [97637800, 317362100], [87251500, 310422300], 
        [82789200, 309534700], [69846100, 294726100], [254081000, 294726100], [285273900, 348753500], 
        [285273900, 461246400], [254081000, 515273900],
    );
    
    # this points belongs to $polyline
    # note: it's actually a vertex, while we should better check an intermediate point
    my $point = Slic3r::Point->new(104577600, 327748400);
    
    local $Slic3r::Geometry::epsilon = 1E-5;
    is_deeply Slic3r::Geometry::polygon_segment_having_point($polygon, $point)->pp, 
        [ [107014700, 340000000], [104577600, 327748400] ],
        'polygon_segment_having_point';
}
{
        auto point = Point(736310778.185108, 5017423926.8924);
        auto line = Line(Point((long int) 627484000, (long int) 3695776000), Point((long int) 750000000, (long int)3720147000));
        //is Slic3r::Geometry::point_in_segment($point, $line), 0, 'point_in_segment';
}

// Possible to delete
{
        //my $p1 = [10, 10];
        //my $p2 = [10, 20];
        //my $p3 = [10, 30];
        //my $p4 = [20, 20];
        //my $p5 = [0,  20];
        
        THEN("Points in a line give the correct angles"){
            //is Slic3r::Geometry::angle3points($p2, $p3, $p1),  PI(),   'angle3points';
            //is Slic3r::Geometry::angle3points($p2, $p1, $p3),  PI(),   'angle3points';
        }
        THEN("Left turns give the correct angle"){
            //is Slic3r::Geometry::angle3points($p2, $p4, $p3),  PI()/2, 'angle3points';
            //is Slic3r::Geometry::angle3points($p2, $p1, $p4),  PI()/2, 'angle3points';
        }
        THEN("Right turns give the correct angle"){
            //is Slic3r::Geometry::angle3points($p2, $p3, $p4),  PI()/2*3, 'angle3points';
            //is Slic3r::Geometry::angle3points($p2, $p1, $p5),  PI()/2*3, 'angle3points';
        }
        //my $p1 = [30, 30];
        //my $p2 = [20, 20];
        //my $p3 = [10, 10];
        //my $p4 = [30, 10];
        
        //is Slic3r::Geometry::angle3points($p2, $p1, $p3), PI(),       'angle3points';
        //is Slic3r::Geometry::angle3points($p2, $p1, $p4), PI()/2*3,   'angle3points';
        //is Slic3r::Geometry::angle3points($p2, $p1, $p1), 2*PI(),     'angle3points';
}

SCENARIO("polygon_is_convex works"){
    GIVEN("A square of dimension 10"){
        //my $cw_square = [ [0,0], [0,10], [10,10], [10,0] ];
        THEN("It is not convex clockwise"){
            //is polygon_is_convex($cw_square), 0, 'cw square is not convex';
        }
        THEN("It is convex counter-clockwise"){
            //is polygon_is_convex([ reverse @$cw_square ]), 1, 'ccw square is convex';
        } 

    }
    GIVEN("A concave polygon"){
        //my $convex1 = [ [0,0], [10,0], [10,10], [0,10], [0,6], [4,6], [4,4], [0,4] ];
        THEN("It is concave"){
            //is polygon_is_convex($convex1), 0, 'concave polygon';
        }
    }
}*/


TEST_CASE("Creating a polyline generates the obvious lines", "[Geometry]"){
    Slic3r::Polyline polyline;
    polyline.points = std::vector<Point>({Point(0, 0), Point(10, 0), Point(20, 0)});
    REQUIRE(polyline.lines().at(0).a == Point(0,0));
    REQUIRE(polyline.lines().at(0).b == Point(10,0));
    REQUIRE(polyline.lines().at(1).a == Point(10,0));
    REQUIRE(polyline.lines().at(1).b == Point(20,0));
}

TEST_CASE("Splitting a Polygon generates a polyline correctly", "[Geometry]"){
    Slic3r::Polygon polygon(std::vector<Point>({Point(0, 0), Point(10, 0), Point(5, 5)}));
    Slic3r::Polyline split = polygon.split_at_index(1);
    REQUIRE(split.points[0]==Point(10,0));
    REQUIRE(split.points[1]==Point(5,5));
    REQUIRE(split.points[2]==Point(0,0));
    REQUIRE(split.points[3]==Point(10,0));
}


TEST_CASE("Bounding boxes are scaled appropriately", "[Geometry]"){
    BoundingBox bb(std::vector<Point>({Point(0, 1), Point(10, 2), Point(20, 2)}));
    bb.scale(2);
    REQUIRE(bb.min == Point(0,2));
    REQUIRE(bb.max == Point(40,4));
}


TEST_CASE("Offseting a line generates a polygon correctly", "[Geometry]"){
	Slic3r::Polyline tmp = { Point(10,10), Point(20,10) };
    Slic3r::Polygon area = offset(tmp,5).at(0);
    REQUIRE(area.area() == Slic3r::Polygon(std::vector<Point>({Point(10,5),Point(20,5),Point(20,15),Point(10,15)})).area());
}

SCENARIO("Circle Fit, TaubinFit with Newton's method", "[Geometry]") {
    GIVEN("A vector of Vec2ds arranged in a half-circle with approximately the same distance R from some point") {
        Vec2d expected_center(-6, 0);
        Vec2ds sample {Vec2d(6.0, 0), Vec2d(5.1961524, 3), Vec2d(3 ,5.1961524), Vec2d(0, 6.0), Vec2d(3, 5.1961524), Vec2d(-5.1961524, 3), Vec2d(-6.0, 0)};
        std::transform(sample.begin(), sample.end(), sample.begin(), [expected_center] (const Vec2d& a) { return a + expected_center;});

        WHEN("Circle fit is called on the entire array") {
            Vec2d result_center(0,0);
            result_center = Geometry::circle_center_taubin_newton(sample);
            THEN("A center point of -6,0 is returned.") {
                REQUIRE(is_approx(result_center, expected_center));
            }
        }
        WHEN("Circle fit is called on the first four points") {
            Vec2d result_center(0,0);
            result_center = Geometry::circle_center_taubin_newton(sample.cbegin(), sample.cbegin()+4);
            THEN("A center point of -6,0 is returned.") {
                REQUIRE(is_approx(result_center, expected_center));
            }
        }
        WHEN("Circle fit is called on the middle four points") {
            Vec2d result_center(0,0);
            result_center = Geometry::circle_center_taubin_newton(sample.cbegin()+2, sample.cbegin()+6);
            THEN("A center point of -6,0 is returned.") {
                REQUIRE(is_approx(result_center, expected_center));
            }
        }
    }
    GIVEN("A vector of Vec2ds arranged in a half-circle with approximately the same distance R from some point") {
        Vec2d expected_center(-3, 9);
        Vec2ds sample {Vec2d(6.0, 0), Vec2d(5.1961524, 3), Vec2d(3 ,5.1961524), 
                        Vec2d(0, 6.0), 
                        Vec2d(3, 5.1961524), Vec2d(-5.1961524, 3), Vec2d(-6.0, 0)};

        std::transform(sample.begin(), sample.end(), sample.begin(), [expected_center] (const Vec2d& a) { return a + expected_center;});


        WHEN("Circle fit is called on the entire array") {
            Vec2d result_center(0,0);
            result_center = Geometry::circle_center_taubin_newton(sample);
            THEN("A center point of 3,9 is returned.") {
                REQUIRE(is_approx(result_center, expected_center));
            }
        }
        WHEN("Circle fit is called on the first four points") {
            Vec2d result_center(0,0);
            result_center = Geometry::circle_center_taubin_newton(sample.cbegin(), sample.cbegin()+4);
            THEN("A center point of 3,9 is returned.") {
                REQUIRE(is_approx(result_center, expected_center));
            }
        }
        WHEN("Circle fit is called on the middle four points") {
            Vec2d result_center(0,0);
            result_center = Geometry::circle_center_taubin_newton(sample.cbegin()+2, sample.cbegin()+6);
            THEN("A center point of 3,9 is returned.") {
                REQUIRE(is_approx(result_center, expected_center));
            }
        }
    }
    GIVEN("A vector of Points arranged in a half-circle with approximately the same distance R from some point") {
        Point expected_center { Point::new_scale(-3, 9)};
        Points sample {Point::new_scale(6.0, 0), Point::new_scale(5.1961524, 3), Point::new_scale(3 ,5.1961524), 
                        Point::new_scale(0, 6.0), 
                        Point::new_scale(3, 5.1961524), Point::new_scale(-5.1961524, 3), Point::new_scale(-6.0, 0)};

        std::transform(sample.begin(), sample.end(), sample.begin(), [expected_center] (const Point& a) { return a + expected_center;});


        WHEN("Circle fit is called on the entire array") {
            Point result_center(0,0);
            result_center = Geometry::circle_center_taubin_newton(sample);
            THEN("A center point of scaled 3,9 is returned.") {
                REQUIRE(is_approx(result_center, expected_center));
            }
        }
        WHEN("Circle fit is called on the first four points") {
            Point result_center(0,0);
            result_center = Geometry::circle_center_taubin_newton(sample.cbegin(), sample.cbegin()+4);
            THEN("A center point of scaled 3,9 is returned.") {
                REQUIRE(is_approx(result_center, expected_center));
            }
        }
        WHEN("Circle fit is called on the middle four points") {
            Point result_center(0,0);
            result_center = Geometry::circle_center_taubin_newton(sample.cbegin()+2, sample.cbegin()+6);
            THEN("A center point of scaled 3,9 is returned.") {
                REQUIRE(is_approx(result_center, expected_center));
            }
        }
    }
}

SCENARIO("Path chaining", "[Geometry]") {
	GIVEN("A path") {
		std::vector<Point> points = { Point(26,26),Point(52,26),Point(0,26),Point(26,52),Point(26,0),Point(0,52),Point(52,52),Point(52,0) };
		THEN("Chained with no diagonals (thus 26 units long)") {
			std::vector<Points::size_type> indices = chain_points(points);
			for (Points::size_type i = 0; i + 1 < indices.size(); ++ i) {
				double dist = (points.at(indices.at(i)).cast<double>() - points.at(indices.at(i+1)).cast<double>()).norm();
				REQUIRE(std::abs(dist-26) <= EPSILON);
			}
		}
	}
	GIVEN("Gyroid infill end points") {
		Polylines polylines = {
			{ {28122608, 3221037}, {27919139, 56036027} },
			{ {33642863, 3400772}, {30875220, 56450360} },
			{ {34579315, 3599827}, {35049758, 55971572} },
			{ {26483070, 3374004}, {23971830, 55763598} },
			{ {38931405, 4678879}, {38740053, 55077714} },
			{ {20311895, 5015778}, {20079051, 54551952} },
			{ {16463068, 6773342}, {18823514, 53992958} },
			{ {44433771, 7424951}, {42629462, 53346059} },
			{ {15697614, 7329492}, {15350896, 52089991} },
			{ {48085792, 10147132}, {46435427, 50792118} },
			{ {48828819, 10972330}, {49126582, 48368374} },
			{ {9654526, 12656711}, {10264020, 47691584} },
			{ {5726905, 18648632}, {8070762, 45082416} },
			{ {54818187, 39579970}, {52974912, 43271272} }, 
			{ {4464342, 37371742}, {5027890, 39106220} },
			{ {54139746, 18417661}, {55177987, 38472580} }, 
			{ {56527590, 32058461}, {56316456, 34067185} },
			{ {3303988, 29215290}, {3569863, 32985633} },
			{ {56255666, 25025857}, {56478310, 27144087} }, 
			{ {4300034, 22805361}, {3667946, 25752601} },
			{ {8266122, 14250611}, {6244813, 17751595} },
			{ {12177955, 9886741}, {10703348, 11491900} } 
		};
		Polylines chained = chain_polylines(polylines);
		THEN("Chained taking the shortest path") {
			double connection_length = 0.;
			for (size_t i = 1; i < chained.size(); ++i) {
				const Polyline &pl1 = chained[i - 1];
				const Polyline &pl2 = chained[i];
				connection_length += (pl2.first_point() - pl1.last_point()).cast<double>().norm();
			}
			REQUIRE(connection_length < 85206000.);
		}
	}
	GIVEN("Loop pieces") {
		Point a { 2185796, 19058485 };
		Point b { 3957902, 18149382 };
		Point c { 2912841, 18790564 };
		Point d { 2831848, 18832390 };
		Point e { 3179601, 18627769 };
		Point f { 3137952, 18653370 };
		Polylines polylines = { { a, b },
								{ c, d },
								{ e, f },
								{ d, a },
								{ f, c },
								{ b, e } };
		Polylines chained = chain_polylines(polylines, &a);
		THEN("Connected without a gap") {
			for (size_t i = 0; i < chained.size(); ++i) {
				const Polyline &pl1 = (i == 0) ? chained.back() : chained[i - 1];
				const Polyline &pl2 = chained[i];
				REQUIRE(pl1.points.back() == pl2.points.front());
			}
		}
	}
}

SCENARIO("Line distances", "[Geometry]"){
    GIVEN("A line"){
        Line line(Point(0, 0), Point(20, 0));
        THEN("Points on the line segment have 0 distance"){
            REQUIRE(line.distance_to(Point(0, 0))  == 0);
            REQUIRE(line.distance_to(Point(20, 0)) == 0);
            REQUIRE(line.distance_to(Point(10, 0)) == 0);
        
        }
        THEN("Points off the line have the appropriate distance"){
            REQUIRE(line.distance_to(Point(10, 10)) == 10);
            REQUIRE(line.distance_to(Point(50, 0)) == 30);
        }
    }
}

SCENARIO("Polygon convex/concave detection", "[Geometry]"){
    GIVEN(("A Square with dimension 100")){
        auto square = Slic3r::Polygon /*new_scale*/(std::vector<Point>({
            Point(100,100),
            Point(200,100),
            Point(200,200),
            Point(100,200)}));
        THEN("It has 4 convex points counterclockwise"){
            REQUIRE(square.concave_points(PI*4/3).size() == 0);
            REQUIRE(square.convex_points(PI*2/3).size() == 4);
        }
        THEN("It has 4 concave points clockwise"){
            square.make_clockwise();
            REQUIRE(square.concave_points(PI*4/3).size() == 4);
            REQUIRE(square.convex_points(PI*2/3).size() == 0);
        }
    }
    GIVEN("A Square with an extra colinearvertex"){
        auto square = Slic3r::Polygon /*new_scale*/(std::vector<Point>({
            Point(150,100),
            Point(200,100),
            Point(200,200),
            Point(100,200),
            Point(100,100)}));
        THEN("It has 4 convex points counterclockwise"){
            REQUIRE(square.concave_points(PI*4/3).size() == 0);
            REQUIRE(square.convex_points(PI*2/3).size() == 4);
        }
    }
    GIVEN("A Square with an extra collinear vertex in different order"){
        auto square = Slic3r::Polygon /*new_scale*/(std::vector<Point>({
            Point(200,200),
            Point(100,200),
            Point(100,100),
            Point(150,100),
            Point(200,100)}));
        THEN("It has 4 convex points counterclockwise"){
            REQUIRE(square.concave_points(PI*4/3).size() == 0);
            REQUIRE(square.convex_points(PI*2/3).size() == 4);
        }
    }

    GIVEN("A triangle"){
        auto triangle = Slic3r::Polygon(std::vector<Point>({
            Point(16000170,26257364),
            Point(714223,461012),
            Point(31286371,461008)
        }));
        THEN("it has three convex vertices"){
            REQUIRE(triangle.concave_points(PI*4/3).size() == 0);
            REQUIRE(triangle.convex_points(PI*2/3).size() == 3);
        }
    }

    GIVEN("A triangle with an extra collinear point"){
        auto triangle = Slic3r::Polygon(std::vector<Point>({
            Point(16000170,26257364),
            Point(714223,461012),
            Point(20000000,461012),
            Point(31286371,461012)
        }));
        THEN("it has three convex vertices"){
            REQUIRE(triangle.concave_points(PI*4/3).size() == 0);
            REQUIRE(triangle.convex_points(PI*2/3).size() == 3);
        }
    }
    GIVEN("A polygon with concave vertices with angles of specifically 4/3pi"){
        // Two concave vertices of this polygon have angle = PI*4/3, so this test fails
        // if epsilon is not used.
        auto polygon = Slic3r::Polygon(std::vector<Point>({
            Point(60246458,14802768),Point(64477191,12360001),
            Point(63727343,11060995),Point(64086449,10853608),
            Point(66393722,14850069),Point(66034704,15057334),
            Point(65284646,13758387),Point(61053864,16200839),
            Point(69200258,30310849),Point(62172547,42483120),
            Point(61137680,41850279),Point(67799985,30310848),
            Point(51399866,1905506),Point(38092663,1905506),
            Point(38092663,692699),Point(52100125,692699)
        }));
        THEN("the correct number of points are detected"){
            REQUIRE(polygon.concave_points(PI*4/3).size() == 6);
            REQUIRE(polygon.convex_points(PI*2/3).size() == 10);
        }
    }
}

TEST_CASE("Triangle Simplification does not result in less than 3 points", "[Geometry]"){
    auto triangle = Slic3r::Polygon(std::vector<Point>({
        Point(16000170,26257364), Point(714223,461012), Point(31286371,461008)
    }));
    REQUIRE(triangle.simplify(250000).at(0).points.size() == 3);
}

SCENARIO("Ported from xs/t/14_geometry.t", "[Geometry]"){
    GIVEN(("square")){
    	Slic3r::Points points { { 100, 100 }, {100, 200 }, { 200, 200 }, { 200, 100 }, { 150, 150 } };
		Slic3r::Polygon hull = Slic3r::Geometry::convex_hull(points);
		SECTION("convex hull returns the correct number of points") { REQUIRE(hull.points.size() == 4); }
    }
	SECTION("arrange returns expected number of positions") {
		Pointfs positions;
		Slic3r::Geometry::arrange(4, Vec2d(20, 20), 5, nullptr, positions);
    	REQUIRE(positions.size() == 4);
    }
	SECTION("directions_parallel") {
    	REQUIRE(Slic3r::Geometry::directions_parallel(0, 0, 0)); 
    	REQUIRE(Slic3r::Geometry::directions_parallel(0, M_PI, 0)); 
    	REQUIRE(Slic3r::Geometry::directions_parallel(0, 0, M_PI / 180));
    	REQUIRE(Slic3r::Geometry::directions_parallel(0, M_PI, M_PI / 180));
    	REQUIRE(! Slic3r::Geometry::directions_parallel(M_PI /2, M_PI, 0));
    	REQUIRE(! Slic3r::Geometry::directions_parallel(M_PI /2, PI, M_PI /180));
    }
}

TEST_CASE("Convex polygon intersection on two disjoint squares", "[Geometry][Rotcalip]") {
    Polygon A{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    A.scale(1. / SCALING_FACTOR);

    Polygon B = A;
    B.translate(20 / SCALING_FACTOR, 0);

    bool is_inters = Geometry::convex_polygons_intersect(A, B);

    REQUIRE(is_inters == false);
}

TEST_CASE("Convex polygon intersection on two intersecting squares", "[Geometry][Rotcalip]") {
    Polygon A{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    A.scale(1. / SCALING_FACTOR);

    Polygon B = A;
    B.translate(5 / SCALING_FACTOR, 5 / SCALING_FACTOR);

    bool is_inters = Geometry::convex_polygons_intersect(A, B);

    REQUIRE(is_inters == true);
}

TEST_CASE("Convex polygon intersection on two squares touching one edge", "[Geometry][Rotcalip]") {
    Polygon A{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    A.scale(1. / SCALING_FACTOR);

    Polygon B = A;
    B.translate(10 / SCALING_FACTOR, 0);

    bool is_inters = Geometry::convex_polygons_intersect(A, B);

    REQUIRE(is_inters == false);
}

TEST_CASE("Convex polygon intersection on two squares touching one vertex", "[Geometry][Rotcalip]") {
    Polygon A{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    A.scale(1. / SCALING_FACTOR);

    Polygon B = A;
    B.translate(10 / SCALING_FACTOR, 10 / SCALING_FACTOR);

    SVG svg{std::string("one_vertex_touch") + ".svg"};
    svg.draw(A, "blue");
    svg.draw(B, "green");
    svg.Close();

    bool is_inters = Geometry::convex_polygons_intersect(A, B);

    REQUIRE(is_inters == false);
}

TEST_CASE("Convex polygon intersection on two overlapping squares", "[Geometry][Rotcalip]") {
    Polygon A{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    A.scale(1. / SCALING_FACTOR);

    Polygon B = A;

    bool is_inters = Geometry::convex_polygons_intersect(A, B);

    REQUIRE(is_inters == true);
}

//// Only for benchmarking
//static Polygon gen_convex_poly(std::mt19937_64 &rg, size_t point_cnt)
//{
//    std::uniform_int_distribution<coord_t> dist(0, 100);

//    Polygon out;
//    out.points.reserve(point_cnt);

//    coord_t tr = dist(rg) * 2 / SCALING_FACTOR;

//    for (size_t i = 0; i < point_cnt; ++i)
//        out.points.emplace_back(tr + dist(rg) / SCALING_FACTOR,
//                                tr + dist(rg) / SCALING_FACTOR);

//    return Geometry::convex_hull(out.points);
//}
//TEST_CASE("Convex polygon intersection test on random polygons", "[Geometry]") {
//    constexpr size_t TEST_CNT = 1000;
//    constexpr size_t POINT_CNT = 1000;

//    auto seed = std::random_device{}();
////    unsigned long seed = 2525634386;
//    std::mt19937_64 rg{seed};
//    Benchmark bench;

//    auto tests = reserve_vector<std::pair<Polygon, Polygon>>(TEST_CNT);
//    auto results = reserve_vector<bool>(TEST_CNT);
//    auto expects = reserve_vector<bool>(TEST_CNT);

//    for (size_t i = 0; i < TEST_CNT; ++i) {
//        tests.emplace_back(gen_convex_poly(rg, POINT_CNT), gen_convex_poly(rg, POINT_CNT));
//    }

//    bench.start();
//    for (const auto &test : tests)
//        results.emplace_back(Geometry::convex_polygons_intersect(test.first, test.second));
//    bench.stop();

//    std::cout << "Test time: " << bench.getElapsedSec() << std::endl;

//    bench.start();
//    for (const auto &test : tests)
//        expects.emplace_back(!intersection(test.first, test.second).empty());
//    bench.stop();

//    std::cout << "Clipper time: " << bench.getElapsedSec() << std::endl;

//    REQUIRE(results.size() == expects.size());

//    auto seedstr = std::to_string(seed);
//    for (size_t i = 0; i < results.size(); ++i) {
//        // std::cout << expects[i] << " ";

//        if (results[i] != expects[i]) {
//            SVG svg{std::string("fail_seed") + seedstr + "_" + std::to_string(i) + ".svg"};
//            svg.draw(tests[i].first, "blue");
//            svg.draw(tests[i].second, "green");
//            svg.Close();

//            // std::cout << std::endl;
//        }
//        REQUIRE(results[i] == expects[i]);
//    }
//    std::cout << std::endl;

//}

struct Pair
{
    size_t first, second;
    bool operator==(const Pair &b) const { return first == b.first && second == b.second; }
};

template<> struct std::hash<Pair> {
    size_t operator()(const Pair &c) const
    {
        return c.first * PRINTER_PART_POLYGONS.size() + c.second;
    }
};

TEST_CASE("Convex polygon intersection test prusa polygons", "[Geometry][Rotcalip]") {

    // Overlap of the same polygon should always be an intersection
    for (size_t i = 0; i < PRINTER_PART_POLYGONS.size(); ++i) {
        Polygon P = PRINTER_PART_POLYGONS[i];
        P = Geometry::convex_hull(P.points);
        bool res = Geometry::convex_polygons_intersect(P, P);
        if (!res) {
            SVG svg{std::string("fail_self") + std::to_string(i) + ".svg"};
            svg.draw(P, "green");
            svg.Close();
        }
        REQUIRE(res == true);
    }

    std::unordered_set<Pair> combos;
    for (size_t i = 0; i < PRINTER_PART_POLYGONS.size(); ++i) {
        for (size_t j = 0; j < PRINTER_PART_POLYGONS.size(); ++j) {
            if (i != j) {
                size_t a = std::min(i, j), b = std::max(i, j);
                combos.insert(Pair{a, b});
            }
        }
    }

    // All disjoint
    for (const auto &combo : combos) {
        Polygon A = PRINTER_PART_POLYGONS[combo.first], B = PRINTER_PART_POLYGONS[combo.second];
        A = Geometry::convex_hull(A.points);
        B = Geometry::convex_hull(B.points);

        auto bba = A.bounding_box();
        auto bbb = B.bounding_box();

        A.translate(-bba.center());
        B.translate(-bbb.center());

        B.translate(bba.size() + bbb.size());

        bool res = Geometry::convex_polygons_intersect(A, B);
        bool ref = !intersection(A, B).empty();

        if (res != ref) {
            SVG svg{std::string("fail") + std::to_string(combo.first) + "_" + std::to_string(combo.second) + ".svg"};
            svg.draw(A, "blue");
            svg.draw(B, "green");
            svg.Close();
        }

        REQUIRE(res == ref);
    }

    // All intersecting
    for (const auto &combo : combos) {
        Polygon A = PRINTER_PART_POLYGONS[combo.first], B = PRINTER_PART_POLYGONS[combo.second];
        A = Geometry::convex_hull(A.points);
        B = Geometry::convex_hull(B.points);

        auto bba = A.bounding_box();
        auto bbb = B.bounding_box();

        A.translate(-bba.center());
        B.translate(-bbb.center());

        bool res = Geometry::convex_polygons_intersect(A, B);
        bool ref = !intersection(A, B).empty();

        if (res != ref) {
            SVG svg{std::string("fail") + std::to_string(combo.first) + "_" + std::to_string(combo.second) + ".svg"};
            svg.draw(A, "blue");
            svg.draw(B, "green");
            svg.Close();
        }

        REQUIRE(res == ref);
    }
}
