#include <functional>

#include "SLABasePool.hpp"
#include "ExPolygon.hpp"
#include "TriangleMesh.hpp"
#include "libnest2d/clipper_backend/clipper_backend.hpp"
#include "ClipperUtils.hpp"

#include "ConcaveHull.hpp"

using BoostPolygon = libnest2d::PolygonImpl;
using BoostPolygons = std::vector<libnest2d::PolygonImpl>;

namespace Slic3r { namespace sla {

namespace {

using coord_t = Point::coord_type;

void reverse(Polygon& p) {
    std::reverse(p.points.begin(), p.points.end());
}

inline BoostPolygon convert(const ExPolygon& exp) {
    auto&& ctour = Slic3rMultiPoint_to_ClipperPath(exp.contour);
    auto&& holes = Slic3rMultiPoints_to_ClipperPaths(exp.holes);
    return {ctour, holes};
}

inline BoostPolygons convert(const ExPolygons& exps) {
    BoostPolygons ret;
    ret.reserve(exps.size());
    std::for_each(exps.begin(), exps.end(), [&ret](const ExPolygon p) {
        ret.emplace_back(convert(p));
    });
    return ret;
}

inline ExPolygon convert(const BoostPolygon& p) {
    ExPolygon ret;

    auto&& ctour = ClipperPath_to_Slic3rPolygon(p.Contour);
    ctour.points.pop_back();

    auto&& holes = ClipperPaths_to_Slic3rPolygons(p.Holes);
    for(auto&& h : holes) h.points.pop_back();

    ret.contour = ctour;
    ret.holes = holes;
    return ret;
}

struct Contour3D {
    Pointf3s points;
    std::vector<Point3> indices;

    void merge(const Contour3D& ctour) {
        auto s3 = coord_t(points.size());
        auto s = coord_t(indices.size());

        points.insert(points.end(), ctour.points.begin(), ctour.points.end());
        indices.insert(indices.end(), ctour.indices.begin(), ctour.indices.end());

        for(auto n = s; n < indices.size(); n++) {
            auto& idx = indices[n]; idx.x += s3; idx.y += s3; idx.z += s3;
        }
    }
};

inline Contour3D convert(const Polygons& triangles, coord_t z, bool dir) {

    Pointf3s points;
    points.reserve(3*triangles.size());
    std::vector<Point3> indices;
    indices.reserve(points.size());

    for(auto& tr : triangles) {
        auto c = coord_t(points.size()), b = c++, a = c++;
        if(dir) indices.emplace_back(a, b, c);
        else indices.emplace_back(c, b, a);
        for(auto& p : tr.points) {
            points.emplace_back(Pointf3::new_unscale(p.x, p.y, z));
        }
    }

    return {points, indices};
}

inline Contour3D roofs(const ExPolygon& poly, coord_t z_distance) {
    Polygons triangles;
    poly.triangulate_pp(&triangles);

    auto lower = convert(triangles, 0, true);
    auto upper = convert(triangles, z_distance, false);
    lower.merge(upper);
    return lower;
}

inline Contour3D inner_bed(const ExPolygon& poly, coord_t depth) {
    Polygons triangles;
    poly.triangulate_pp(&triangles);

    auto bottom = convert(triangles, -depth, false);
    auto lines = poly.lines();

    // Generate outer walls
    auto fp = [](const Point& p, Point::coord_type z) {
        return Pointf3::new_unscale(p.x, p.y, z);
    };

    for(auto& l : lines) {
        auto s = bottom.points.size();

        bottom.points.emplace_back(fp(l.a, -depth));
        bottom.points.emplace_back(fp(l.b, -depth));
        bottom.points.emplace_back(fp(l.a, 0));
        bottom.points.emplace_back(fp(l.b, 0));

        bottom.indices.emplace_back(s, s + 1, s + 3);
        bottom.indices.emplace_back(s, s + 3, s + 2);
    }

    return bottom;
}

inline TriangleMesh mesh(const Contour3D& ctour) {
    return {ctour.points, ctour.indices};
}

inline TriangleMesh mesh(Contour3D&& ctour) {
    return {std::move(ctour.points), std::move(ctour.indices)};
}

inline void offset(BoostPolygon& sh, Point::coord_type distance) {
    using ClipperLib::ClipperOffset;
    using ClipperLib::jtRound;
    using ClipperLib::etClosedPolygon;
    using ClipperLib::Paths;
    using namespace libnest2d;

    // If the input is not at least a triangle, we can not do this algorithm
    if(sh.Contour.size() <= 3 ||
       std::any_of(sh.Holes.begin(), sh.Holes.end(),
                   [](const PathImpl& p) { return p.size() <= 3; })
       ) throw GeometryException(GeomErr::OFFSET);

    ClipperOffset offs;
    Paths result;
    offs.AddPath(sh.Contour, jtRound, etClosedPolygon);
    offs.AddPaths(sh.Holes, jtRound, etClosedPolygon);
    offs.Execute(result, static_cast<double>(distance));

    // Offsetting reverts the orientation and also removes the last vertex
    // so boost will not have a closed polygon.

    bool found_the_contour = false;
    for(auto& r : result) {
        if(ClipperLib::Orientation(r)) {
            // We don't like if the offsetting generates more than one contour
            // but throwing would be an overkill. Instead, we should warn the
            // caller about the inability to create correct geometries
            if(!found_the_contour) {
                sh.Contour = r;
                ClipperLib::ReversePath(sh.Contour);
                sh.Contour.push_back(sh.Contour.front());
                found_the_contour = true;
            } else {
                dout() << "Warning: offsetting result is invalid!";
                /* TODO warning */
            }
        } else {
            // TODO If there are multiple contours we can't be sure which hole
            // belongs to the first contour. (But in this case the situation is
            // bad enough to let it go...)
            sh.Holes.push_back(r);
            ClipperLib::ReversePath(sh.Holes.back());
            sh.Holes.back().push_back(sh.Holes.back().front());
        }
    }
}


inline ExPolygon concave_hull(const ExPolygons& polys) {
    concavehull::PointVector pv;
    size_t s = 0;

    for(auto& ep : polys) s += ep.contour.points.size();
    pv.reserve(s);

    std::cout << polys.size() << std::endl;

//    for(const ExPolygon& ep : polys) {
    auto& ep = polys.front();

    for(auto& v : ep.contour.points)
        pv.emplace_back(Pointf::new_unscale(v.x, v.y));

    std::reverse(pv.begin(), pv.end());

//        auto frontpoint = ep.contour.points.front();
//        pv.emplace_back(Pointf::new_unscale(frontpoint));
//    }

    auto result = concavehull::ConcaveHull(pv, 3, true);

    if(result.empty()) std::cout << "Empty concave hull!!!" << std::endl;
    std::cout << "result size " << result.size() << std::endl;

    ExPolygon ret;
    ret.contour.points.reserve(result.size() + 1);

    std::reverse(result.begin(), result.end());

    for(auto& p : result) {
        std::cout << p.x << " " << p.y << std::endl;
        ret.contour.points.emplace_back(Point::new_scale(p.x, p.y));
    }

    reverse(ret.contour);

//    ret.contour.points.emplace_back(ret.contour.points.front());

    return ret;
}

}

void ground_layer(const TriangleMesh &mesh, ExPolygons &output, float h)
{
    TriangleMesh m = mesh;
    TriangleMeshSlicer slicer(&m);

    std::vector<ExPolygons> tmp;

    slicer.slice({h}, &tmp);

    output = tmp.front();
}

void create_base_pool(const ExPolygons &ground_layer, TriangleMesh& out)
{
    using libnest2d::PolygonImpl;
    using boost::geometry::convex_hull;
    using boost::geometry::is_valid;

    static const Point::coord_type INNER_OFFSET_DIST = 2000000;
    static const Point::coord_type OFFSET_DIST = 5000000;
    static const Point::coord_type HEIGHT = 10000000;

    // 1: Offset the ground layer
    auto concaveh = ground_layer.front(); //concave_hull(ground_layer);
    concaveh.holes.clear();

//    BoostPolygon chull_boost;
//    convex_hull(convert(ground_layer), chull_boost);
//    auto concaveh = convert(chull_boost);

//    auto pool = roofs(concaveh, HEIGHT);

//    // Generate outer walls
//    auto fp = [](const Point& p, Point::coord_type z) {
//        return Pointf3::new_unscale(p.x, p.y, z);
//    };

//    auto lines = concaveh.lines();
//    std::cout << "lines: " << lines.size() << std::endl;
//    for(auto& l : lines) {
//        auto s = pool.points.size();

//        pool.points.emplace_back(fp(l.a, 0));
//        pool.points.emplace_back(fp(l.b, 0));
//        pool.points.emplace_back(fp(l.a, HEIGHT));
//        pool.points.emplace_back(fp(l.b, HEIGHT));

//        pool.indices.emplace_back(s, s + 3, s + 1);
//        pool.indices.emplace_back(s, s + 2, s + 3);
//    }

//    out = mesh(pool);

    BoostPolygon chull_boost = convert(concaveh);
//    convex_hull(convert(ground_layer), chull_boost);

    offset(chull_boost, INNER_OFFSET_DIST);
    auto chull_outer_boost = chull_boost;
    offset(chull_outer_boost, OFFSET_DIST);


    // Convert back to Slic3r format
    ExPolygon chull_inner = convert(chull_boost);
    ExPolygon chull_outer = convert(chull_outer_boost);

    ExPolygon top_poly;
    top_poly.contour = chull_outer.contour;
    top_poly.holes.emplace_back(chull_inner.contour);
    reverse(top_poly.holes.back());

    Contour3D pool;

    Polygons top_triangles, bottom_triangles;
    top_poly.triangulate_pp(&top_triangles);
    chull_outer.triangulate_pp(&bottom_triangles);
    auto top_plate = convert(top_triangles, 0, false);
    auto bottom_plate = convert(bottom_triangles, -HEIGHT, true);
    auto innerbed = inner_bed(chull_inner, HEIGHT/2);

    // Generate outer walls
    auto fp = [](const Point& p, Point::coord_type z) {
        return Pointf3::new_unscale(p.x, p.y, z);
    };

    auto lines = chull_outer.lines();
    for(auto& l : lines) {
        auto s = pool.points.size();

        pool.points.emplace_back(fp(l.a, -HEIGHT));
        pool.points.emplace_back(fp(l.b, -HEIGHT));
        pool.points.emplace_back(fp(l.a, 0));
        pool.points.emplace_back(fp(l.b, 0));

        pool.indices.emplace_back(s, s + 3, s + 1);
        pool.indices.emplace_back(s, s + 2, s + 3);
    }

    pool.merge(top_plate);
    pool.merge(bottom_plate);
    pool.merge(innerbed);

    out = mesh(pool);
}

}
}
