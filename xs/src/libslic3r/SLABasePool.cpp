#include <functional>
#include <numeric>

#include "SLABasePool.hpp"
#include "ExPolygon.hpp"
#include "TriangleMesh.hpp"
#include "ClipperUtils.hpp"
#include "boost/log/trivial.hpp"

//#include "SVG.hpp"

namespace Slic3r { namespace sla {

namespace {

using coord_t = Point::coord_type;

/// get the scaled clipper units for a millimeter value
inline coord_t mm(double v) { return coord_t(v/SCALING_FACTOR); }

/// Get x and y coordinates (because we are eigenizing...)
inline coord_t x(const Point& p) { return p(0); }
inline coord_t y(const Point& p) { return p(1); }
inline coord_t& x(Point& p) { return p(0); }
inline coord_t& y(Point& p) { return p(1); }

inline coordf_t x(const Vec3d& p) { return p(0); }
inline coordf_t y(const Vec3d& p) { return p(1); }
inline coordf_t z(const Vec3d& p) { return p(2); }
inline coordf_t& x(Vec3d& p) { return p(0); }
inline coordf_t& y(Vec3d& p) { return p(1); }
inline coordf_t& z(Vec3d& p) { return p(2); }

inline coord_t& x(Vec3crd& p) { return p(0); }
inline coord_t& y(Vec3crd& p) { return p(1); }
inline coord_t& z(Vec3crd& p) { return p(2); }
inline coord_t x(const Vec3crd& p) { return p(0); }
inline coord_t y(const Vec3crd& p) { return p(1); }
inline coord_t z(const Vec3crd& p) { return p(2); }

inline void triangulate(const ExPolygon& expoly, Polygons& triangles) {
    expoly.triangulate_p2t(&triangles);
}

inline Polygons triangulate(const ExPolygon& expoly) {
    Polygons tri; triangulate(expoly, tri); return tri;
}

using Indices = std::vector<Vec3crd>;

/// Intermediate struct for a 3D mesh
struct Contour3D {
    Pointf3s points;
    Indices indices;

    void merge(const Contour3D& ctr) {
        auto s3 = coord_t(points.size());
        auto s = coord_t(indices.size());

        points.insert(points.end(), ctr.points.begin(), ctr.points.end());
        indices.insert(indices.end(), ctr.indices.begin(), ctr.indices.end());

        for(auto n = s; n < indices.size(); n++) {
            auto& idx = indices[n]; x(idx) += s3; y(idx) += s3; z(idx) += s3;
        }
    }
};

/// Convert the triangulation output to an intermediate mesh.
inline Contour3D convert(const Polygons& triangles, coord_t z, bool dir) {

    Pointf3s points;
    points.reserve(3*triangles.size());
    Indices indices;
    indices.reserve(points.size());

    for(auto& tr : triangles) {
        auto c = coord_t(points.size()), b = c++, a = c++;
        if(dir) indices.emplace_back(a, b, c);
        else indices.emplace_back(c, b, a);
        for(auto& p : tr.points) {
            points.emplace_back(unscale(x(p), y(p), z));
        }
    }

    return {points, indices};
}

/// Only a debug function to generate top and bottom plates from a 2D shape.
/// It is not used in the algorithm directly.
inline Contour3D roofs(const ExPolygon& poly, coord_t z_distance) {
    Polygons triangles = triangulate(poly);

    auto lower = convert(triangles, 0, false);
    auto upper = convert(triangles, z_distance, true);
    lower.merge(upper);
    return lower;
}

inline Contour3D walls(const ExPolygon& floor_plate, const ExPolygon& ceiling,
                       double floor_z_mm, double ceiling_z_mm) {
    using std::transform; using std::back_inserter;

    ExPolygon poly;
    poly.contour.points = floor_plate.contour.points;
    poly.holes.emplace_back(ceiling.contour);
    auto& h = poly.holes.front();
    std::reverse(h.points.begin(), h.points.end());
    Polygons tri = triangulate(poly);

    Contour3D ret;
    ret.points.reserve(tri.size() * 3);

    double fz = floor_z_mm;
    double cz = ceiling_z_mm;
    auto& rp = ret.points;
    auto& rpi = ret.indices;
    ret.indices.reserve(tri.size() * 3);

    coord_t idx = 0;

    auto hlines = h.lines();
    auto is_upper = [&hlines](const Point& p) {
        return std::any_of(hlines.begin(), hlines.end(),
                               [&p](const Line& l) {
            return l.distance_to(p) < mm(0.01);
        });
    };

    std::for_each(tri.begin(), tri.end(),
                  [&rp, &rpi, &poly, &idx, is_upper, fz, cz](const Polygon& pp)
    {
        for(auto& p : pp.points)
            if(is_upper(p))
                rp.emplace_back(unscale(x(p), y(p), mm(cz)));
            else rp.emplace_back(unscale(x(p), y(p), mm(fz)));

        coord_t a = idx++, b = idx++, c = idx++;
        if(fz > cz) rpi.emplace_back(c, b, a);
        else rpi.emplace_back(a, b, c);
    });

    return ret;
}

/// Mesh from an existing contour.
inline TriangleMesh mesh(const Contour3D& ctour) {
    return {ctour.points, ctour.indices};
}

/// Mesh from an evaporating 3D contour
inline TriangleMesh mesh(Contour3D&& ctour) {
    return {std::move(ctour.points), std::move(ctour.indices)};
}

/// Offsetting with clipper and smoothing the edges into a curvature.
inline void offset(ExPolygon& sh, coord_t distance) {
    using ClipperLib::ClipperOffset;
    using ClipperLib::jtRound;
    using ClipperLib::etClosedPolygon;
    using ClipperLib::Paths;
    using ClipperLib::Path;

    auto&& ctour = Slic3rMultiPoint_to_ClipperPath(sh.contour);
    auto&& holes = Slic3rMultiPoints_to_ClipperPaths(sh.holes);

    // If the input is not at least a triangle, we can not do this algorithm
    if(ctour.size() < 3 ||
       std::any_of(holes.begin(), holes.end(),
                   [](const Path& p) { return p.size() < 3; })
            ) {
        BOOST_LOG_TRIVIAL(error) << "Invalid geometry for offsetting!";
        return;
    }

    ClipperOffset offs;
    offs.ArcTolerance = 0.01*mm(1);
    Paths result;
    offs.AddPath(ctour, jtRound, etClosedPolygon);
    offs.AddPaths(holes, jtRound, etClosedPolygon);
    offs.Execute(result, static_cast<double>(distance));

    // Offsetting reverts the orientation and also removes the last vertex
    // so boost will not have a closed polygon.

    bool found_the_contour = false;
    sh.holes.clear();
    for(auto& r : result) {
        if(ClipperLib::Orientation(r)) {
            // We don't like if the offsetting generates more than one contour
            // but throwing would be an overkill. Instead, we should warn the
            // caller about the inability to create correct geometries
            if(!found_the_contour) {
                auto rr = ClipperPath_to_Slic3rPolygon(r);
                sh.contour.points.swap(rr.points);
                found_the_contour = true;
            } else {
                BOOST_LOG_TRIVIAL(warning)
                        << "Warning: offsetting result is invalid!";
            }
        } else {
            // TODO If there are multiple contours we can't be sure which hole
            // belongs to the first contour. (But in this case the situation is
            // bad enough to let it go...)
            sh.holes.emplace_back(ClipperPath_to_Slic3rPolygon(r));
        }
    }
}

template<class ExP, class D>
inline Contour3D round_edges(const ExPolygon& base_plate,
                            double radius_mm,
                            double degrees,
                            double ceilheight_mm,
                            bool dir,
                            ExP&& last_offset = ExP(), D&& last_height = D())
{
    auto ob = base_plate;
    auto ob_prev = ob;
    double wh = ceilheight_mm, wh_prev = wh;
    Contour3D curvedwalls;

    const size_t steps = 6;     // steps for 180 degrees
    degrees = std::fmod(degrees, 180);
    const int portion = int(steps*degrees / 90);
    const double ystep_mm = radius_mm/steps;
    coord_t s = dir? 1 : -1;
    double xxprev = 0;
    for(int i = 0; i < portion; i++) {
        ob = base_plate;

        // The offset is given by the equation: x = sqrt(r^2 - y^2)
        // which can be derived from the circle equation. y is the current
        // height for which the offset is calculated and x is the offset itself
        // r is the radius of the circle that is used to smooth the edges

        double r2 = radius_mm * radius_mm;
        double y2 = steps*ystep_mm - i*ystep_mm;
        y2 *= y2;

        double xx = sqrt(r2 - y2);

        offset(ob, s*mm(xx));
        wh = ceilheight_mm - i*ystep_mm;

        Contour3D pwalls;
        if(xxprev < xx) pwalls = walls(ob, ob_prev, wh, wh_prev);
        else pwalls = walls(ob_prev, ob, wh_prev, wh);

        curvedwalls.merge(pwalls);
        ob_prev = ob;
        wh_prev = wh;
        xxprev = xx;
    }

    last_offset = std::move(ob);
    last_height = wh;

    return curvedwalls;
}

/// Generating the concave part of the 3D pool with the bottom plate and the
/// side walls.
inline Contour3D inner_bed(const ExPolygon& poly, double depth_mm,
                           double begin_h_mm = 0) {

    Polygons triangles = triangulate(poly);

    coord_t depth = mm(depth_mm);
    coord_t begin_h = mm(begin_h_mm);

    auto bottom = convert(triangles, -depth + begin_h, false);
    auto lines = poly.lines();

    // Generate outer walls
    auto fp = [](const Point& p, Point::coord_type z) {
        return unscale(x(p), y(p), z);
    };

    for(auto& l : lines) {
        auto s = coord_t(bottom.points.size());

        bottom.points.emplace_back(fp(l.a, -depth + begin_h));
        bottom.points.emplace_back(fp(l.b, -depth + begin_h));
        bottom.points.emplace_back(fp(l.a, begin_h));
        bottom.points.emplace_back(fp(l.b, begin_h));

        bottom.indices.emplace_back(s + 3, s + 1, s);
        bottom.indices.emplace_back(s + 2, s + 3, s);
    }

    return bottom;
}

/// Unification of polygons (with clipper) preserving holes as well.
inline ExPolygons unify(const ExPolygons& shapes) {
    using ClipperLib::ptSubject;

    ExPolygons retv;

    bool closed = true;
    bool valid = true;

    ClipperLib::Clipper clipper;

    for(auto& path : shapes) {
        auto clipperpath = Slic3rMultiPoint_to_ClipperPath(path.contour);

        if(!clipperpath.empty())
            valid &= clipper.AddPath(clipperpath, ptSubject, closed);

        auto clipperholes = Slic3rMultiPoints_to_ClipperPaths(path.holes);

        for(auto& hole : clipperholes) {
            if(!hole.empty())
                valid &= clipper.AddPath(hole, ptSubject, closed);
        }
    }

    if(!valid) BOOST_LOG_TRIVIAL(warning) << "Unification of invalid shapes!";

    ClipperLib::PolyTree result;
    clipper.Execute(ClipperLib::ctUnion, result, ClipperLib::pftNonZero);

    retv.reserve(static_cast<size_t>(result.Total()));

    // Now we will recursively traverse the polygon tree and serialize it
    // into an ExPolygon with holes. The polygon tree has the clipper-ish
    // PolyTree structure which alternates its nodes as contours and holes

    // A "declaration" of function for traversing leafs which are holes
    std::function<void(ClipperLib::PolyNode*, ExPolygon&)> processHole;

    // Process polygon which calls processHoles which than calls processPoly
    // again until no leafs are left.
    auto processPoly = [&retv, &processHole](ClipperLib::PolyNode *pptr) {
        ExPolygon poly;
        poly.contour.points = ClipperPath_to_Slic3rPolygon(pptr->Contour);
        for(auto h : pptr->Childs) { processHole(h, poly); }
        retv.push_back(poly);
    };

    // Body of the processHole function
    processHole = [&processPoly](ClipperLib::PolyNode *pptr, ExPolygon& poly)
    {
        poly.holes.emplace_back();
        poly.holes.back().points = ClipperPath_to_Slic3rPolygon(pptr->Contour);
        for(auto c : pptr->Childs) processPoly(c);
    };

    // Wrapper for traversing.
    auto traverse = [&processPoly] (ClipperLib::PolyNode *node)
    {
        for(auto ch : node->Childs) {
            processPoly(ch);
        }
    };

    // Here is the actual traverse
    traverse(&result);

    return retv;
}

inline Point centroid(Points& pp) {
    Point c;
    switch(pp.size()) {
    case 0: break;
    case 1: c = pp.front(); break;
    case 2: c = (pp[0] + pp[1]) / 2; break;
    default: {
        Polygon p;
        p.points.swap(pp);
        c = p.centroid();
        pp.swap(p.points);
        break;
    }
    }

    return c;
}

inline Point centroid(const ExPolygon& poly) {
    return poly.contour.centroid();
}

/// A fake concave hull that is constructed by connecting separate shapes
/// with explicit bridges. Bridges are generated from each shape's centroid
/// to the center of the "scene" which is the centroid calculated from the shape
/// centroids (a star is created...)
inline ExPolygons concave_hull(const ExPolygons& polys, double max_dist_mm = 50)
{
    if(polys.empty()) return ExPolygons();

    ExPolygons punion = unify(polys);   // could be redundant

    if(punion.size() == 1) return punion;

    // We get the centroids of all the islands in the 2D slice
    Points centroids; centroids.reserve(punion.size());
    std::transform(punion.begin(), punion.end(), std::back_inserter(centroids),
                   [](const ExPolygon& poly) { return centroid(poly); });

    // Centroid of the centroids of islands. This is where the additional
    // connector sticks are routed.
    Point cc = centroid(centroids);

    punion.reserve(punion.size() + centroids.size());

    std::transform(centroids.begin(), centroids.end(),
                   std::back_inserter(punion),
                   [cc, max_dist_mm](const Point& c) {

        double dx = x(c) - x(cc), dy = y(c) - y(cc);
        double l = std::sqrt(dx * dx + dy * dy);
        double nx = dx / l, ny = dy / l;
        double max_dist = mm(max_dist_mm);

        if(l > max_dist) return ExPolygon();

        ExPolygon r;
        auto& ctour = r.contour.points;

        ctour.reserve(3);
        ctour.emplace_back(cc);

        Point d(coord_t(mm(1)*nx), coord_t(mm(1)*ny));
        ctour.emplace_back(c + Point( -y(d),  x(d) ));
        ctour.emplace_back(c + Point(  y(d), -x(d) ));
        offset(r, mm(1));

        return r;
    });

    punion = unify(punion);

    return punion;
}

}

void ground_layer(const TriangleMesh &mesh, ExPolygons &output, float h)
{
    TriangleMesh m = mesh;
    TriangleMeshSlicer slicer(&m);

    std::vector<ExPolygons> tmp;

    slicer.slice({h}, &tmp, [](){});

    output = tmp.front();
}

void create_base_pool(const ExPolygons &ground_layer, TriangleMesh& out,
                      double min_wall_thickness_mm,
                      double min_wall_height_mm,
                      double max_merge_distance_mm)
{
    auto concavehs = concave_hull(ground_layer, max_merge_distance_mm);
    for(ExPolygon& concaveh : concavehs) {
        if(concaveh.contour.points.empty()) return;
        concaveh.holes.clear();

        BoundingBox bb(concaveh);
        coord_t w = x(bb.max) - x(bb.min);
        coord_t h = y(bb.max) - y(bb.min);

        auto wall_thickness = coord_t((w+h)*0.01);

        const coord_t WALL_THICKNESS = mm(min_wall_thickness_mm) +
                                       wall_thickness;

        const coord_t WALL_DISTANCE = coord_t(0.3*WALL_THICKNESS);
        const coord_t HEIGHT = mm(min_wall_height_mm);

        auto outer_base = concaveh;
        offset(outer_base, WALL_THICKNESS+WALL_DISTANCE);
        auto inner_base = outer_base;
        offset(inner_base, -WALL_THICKNESS);
        inner_base.holes.clear(); outer_base.holes.clear();

        ExPolygon top_poly;
        top_poly.contour = outer_base.contour;
        top_poly.holes.emplace_back(inner_base.contour);
        auto& tph = top_poly.holes.back().points;
        std::reverse(tph.begin(), tph.end());

        Contour3D pool;

        ExPolygon ob = outer_base; double wh = 0;
        auto curvedwalls = round_edges(ob,
                                       1,    // radius 1 mm
                                       170,  // 170 degrees
                                       0,    // z position of the input plane
                                       true,
                                       ob, wh);
        pool.merge(curvedwalls);

        ExPolygon ob_contr = ob;
        ob_contr.holes.clear();

        auto pwalls = walls(ob_contr, inner_base, wh, -min_wall_height_mm);
        pool.merge(pwalls);

        Polygons top_triangles, bottom_triangles;
        triangulate(top_poly, top_triangles);
        triangulate(inner_base, bottom_triangles);
        auto top_plate = convert(top_triangles, 0, false);
        auto bottom_plate = convert(bottom_triangles, -HEIGHT, true);

        ob = inner_base; wh = 0;
        curvedwalls = round_edges(ob,
                                  1,    // radius 1 mm
                                  90,  // 170 degrees
                                  0,    // z position of the input plane
                                  false,
                                  ob, wh);
        pool.merge(curvedwalls);

        auto innerbed = inner_bed(ob, min_wall_height_mm/2 + wh, wh);

        pool.merge(top_plate);
        pool.merge(bottom_plate);
        pool.merge(innerbed);

        out.merge(mesh(pool));
    }
}

}
}
