#include "SLABasePool.hpp"
#include "SLABoilerPlate.hpp"

#include "boost/log/trivial.hpp"
#include "SLABoostAdapter.hpp"
#include "ClipperUtils.hpp"

//#include "SVG.hpp"
//#include "benchmark.h"

namespace Slic3r { namespace sla {

/// Convert the triangulation output to an intermediate mesh.
Contour3D convert(const Polygons& triangles, coord_t z, bool dir) {

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

Contour3D walls(const ExPolygon& floor_plate, const ExPolygon& ceiling,
                double floor_z_mm, double ceiling_z_mm,
                ThrowOnCancel thr)
{
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
            return l.distance_to(p) < mm(1e-6);
        });
    };

    std::for_each(tri.begin(), tri.end(),
                  [&rp, &rpi, thr, &idx, is_upper, fz, cz](const Polygon& pp)
    {
        thr(); // may throw if cancellation was requested

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

/// Offsetting with clipper and smoothing the edges into a curvature.
void offset(ExPolygon& sh, coord_t distance) {
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

/// Unification of polygons (with clipper) preserving holes as well.
ExPolygons unify(const ExPolygons& shapes) {
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

/// Only a debug function to generate top and bottom plates from a 2D shape.
/// It is not used in the algorithm directly.
inline Contour3D roofs(const ExPolygon& poly, coord_t z_distance) {
    Polygons triangles = triangulate(poly);

    auto lower = convert(triangles, 0, false);
    auto upper = convert(triangles, z_distance, true);
    lower.merge(upper);
    return lower;
}

template<class ExP, class D>
Contour3D round_edges(const ExPolygon& base_plate,
                      double radius_mm,
                      double degrees,
                      double ceilheight_mm,
                      bool dir,
                      ThrowOnCancel throw_on_cancel,
                      ExP&& last_offset = ExP(), D&& last_height = D())
{
    auto ob = base_plate;
    auto ob_prev = ob;
    double wh = ceilheight_mm, wh_prev = wh;
    Contour3D curvedwalls;

    int steps = 15; // int(std::ceil(10*std::pow(radius_mm, 1.0/3)));
    double stepx = radius_mm / steps;
    coord_t s = dir? 1 : -1;
    degrees = std::fmod(degrees, 180);

    if(degrees >= 90) {
        for(int i = 1; i <= steps; ++i) {
            throw_on_cancel();

            ob = base_plate;

            double r2 = radius_mm * radius_mm;
            double xx = i*stepx;
            double x2 = xx*xx;
            double stepy = std::sqrt(r2 - x2);

            offset(ob, s*mm(xx));
            wh = ceilheight_mm - radius_mm + stepy;

            Contour3D pwalls;
            pwalls = walls(ob, ob_prev, wh, wh_prev, throw_on_cancel);

            curvedwalls.merge(pwalls);
            ob_prev = ob;
            wh_prev = wh;
        }
    }

    double tox = radius_mm - radius_mm*std::sin(degrees * PI / 180);
    int tos = int(tox / stepx);

    for(int i = 1; i <= tos; ++i) {
        throw_on_cancel();
        ob = base_plate;

        double r2 = radius_mm * radius_mm;
        double xx = radius_mm - i*stepx;
        double x2 = xx*xx;
        double stepy = std::sqrt(r2 - x2);
        offset(ob, s*mm(xx));
        wh = ceilheight_mm - radius_mm - stepy;

        Contour3D pwalls;
        pwalls = walls(ob_prev, ob, wh_prev, wh, throw_on_cancel);

        curvedwalls.merge(pwalls);
        ob_prev = ob;
        wh_prev = wh;
    }

    last_offset = std::move(ob);
    last_height = wh;

    return curvedwalls;
}

/// Generating the concave part of the 3D pool with the bottom plate and the
/// side walls.
Contour3D inner_bed(const ExPolygon& poly, double depth_mm,
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

inline Point centroid(Points& pp) {
    Point c;
    switch(pp.size()) {
    case 0: break;
    case 1: c = pp.front(); break;
    case 2: c = (pp[0] + pp[1]) / 2; break;
    default: {
        auto MAX = std::numeric_limits<Point::coord_type>::max();
        auto MIN = std::numeric_limits<Point::coord_type>::min();
        Point min = {MAX, MAX}, max = {MIN, MIN};

        for(auto& p : pp) {
            if(p(0) < min(0)) min(0) = p(0);
            if(p(1) < min(1)) min(1) = p(1);
            if(p(0) > max(0)) max(0) = p(0);
            if(p(1) > max(1)) max(1) = p(1);
        }
        c(0) = min(0) + (max(0) - min(0)) / 2;
        c(1) = min(1) + (max(1) - min(1)) / 2;

        // TODO: fails for non convex cluster
//        c = std::accumulate(pp.begin(), pp.end(), Point{0, 0});
//        x(c) /= coord_t(pp.size()); y(c) /= coord_t(pp.size());
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
ExPolygons concave_hull(const ExPolygons& polys, double max_dist_mm = 50,
                        ThrowOnCancel throw_on_cancel = [](){})
{
    namespace bgi = boost::geometry::index;
    using SpatElement = std::pair<BoundingBox, unsigned>;
    using SpatIndex = bgi::rtree< SpatElement, bgi::rstar<16, 4> >;

    if(polys.empty()) return ExPolygons();

    ExPolygons punion = unify(polys);   // could be redundant

    if(punion.size() == 1) return punion;

    // We get the centroids of all the islands in the 2D slice
    Points centroids; centroids.reserve(punion.size());
    std::transform(punion.begin(), punion.end(), std::back_inserter(centroids),
                   [](const ExPolygon& poly) { return centroid(poly); });


    SpatIndex boxindex; unsigned idx = 0;
    std::for_each(punion.begin(), punion.end(),
                  [&boxindex, &idx](const ExPolygon& expo) {
        BoundingBox bb(expo);
        boxindex.insert(std::make_pair(bb, idx++));
    });


    // Centroid of the centroids of islands. This is where the additional
    // connector sticks are routed.
    Point cc = centroid(centroids);

    punion.reserve(punion.size() + centroids.size());

    idx = 0;
    std::transform(centroids.begin(), centroids.end(),
                   std::back_inserter(punion),
                   [&punion, &boxindex, cc, max_dist_mm, &idx, throw_on_cancel]
                   (const Point& c)
    {
        throw_on_cancel();
        double dx = x(c) - x(cc), dy = y(c) - y(cc);
        double l = std::sqrt(dx * dx + dy * dy);
        double nx = dx / l, ny = dy / l;
        double max_dist = mm(max_dist_mm);

        ExPolygon& expo = punion[idx++];
        BoundingBox querybb(expo);

        querybb.offset(max_dist);
        std::vector<SpatElement> result;
        boxindex.query(bgi::intersects(querybb), std::back_inserter(result));
        if(result.size() <= 1) return ExPolygon();

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

void base_plate(const TriangleMesh &mesh, ExPolygons &output, float h,
                float layerh, ThrowOnCancel thrfn)
{
    TriangleMesh m = mesh;
    TriangleMeshSlicer slicer(&m);

    auto bb = mesh.bounding_box();
    float gnd = float(bb.min(Z));
    std::vector<float> heights = {float(bb.min(Z))};
    for(float hi = gnd + layerh; hi <= gnd + h; hi += layerh)
        heights.emplace_back(hi);

    std::vector<ExPolygons> out; out.reserve(size_t(std::ceil(h/layerh)));
    slicer.slice(heights, &out, thrfn);

    size_t count = 0; for(auto& o : out) count += o.size();
    ExPolygons tmp; tmp.reserve(count);
    for(auto& o : out) for(auto& e : o) tmp.emplace_back(std::move(e));

    ExPolygons utmp = unify(tmp);
    for(auto& o : utmp) {
        auto&& smp = o.simplify(0.1/SCALING_FACTOR);
        output.insert(output.end(), smp.begin(), smp.end());
    }
}

void create_base_pool(const ExPolygons &ground_layer, TriangleMesh& out,
                      const PoolConfig& cfg)
{

    double mergedist = 2*(1.8*cfg.min_wall_thickness_mm + 4*cfg.edge_radius_mm)+
                       cfg.max_merge_distance_mm;

    auto concavehs = concave_hull(ground_layer, mergedist, cfg.throw_on_cancel);


    for(ExPolygon& concaveh : concavehs) {
        if(concaveh.contour.points.empty()) return;
        concaveh.holes.clear();

        const double thickness      = cfg.min_wall_thickness_mm;
        const double wingheight     = cfg.min_wall_height_mm;
        const double fullheight     = wingheight + thickness;
        const double tilt = PI/4;
        const double wingdist       = wingheight / std::tan(tilt);

        // scaled values
        const coord_t s_thickness   = mm(thickness);
        const coord_t s_eradius     = mm(cfg.edge_radius_mm);
        const coord_t s_safety_dist = 2*s_eradius + coord_t(0.8*s_thickness);
        // const coord_t wheight    = mm(cfg.min_wall_height_mm);
        coord_t s_wingdist          = mm(wingdist);

        // Here lies the trick that does the smooting only with clipper offset
        // calls. The offset is configured to round edges. Inner edges will
        // be rounded because we offset twice: ones to get the outer (top) plate
        // and again to get the inner (bottom) plate
        auto outer_base = concaveh;
        offset(outer_base, s_safety_dist + s_wingdist + s_thickness);
        auto inner_base = outer_base;
        auto middle_base = outer_base;
        offset(inner_base, -(s_thickness + s_wingdist));
        offset(middle_base, -s_thickness);
        inner_base.holes.clear();   // bottom contour
        middle_base.holes.clear();  // contour of the cavity-top
        outer_base.holes.clear();   // bottom contour, also for the cavity

        // Punching a hole in the top plate for the cavity
        ExPolygon top_poly;
        top_poly.contour = outer_base.contour;
        top_poly.holes.emplace_back(middle_base.contour);
        auto& tph = top_poly.holes.back().points;
        std::reverse(tph.begin(), tph.end());

        Contour3D pool;

        ExPolygon ob = outer_base; double wh = 0;

        // now we will calculate the angle or portion of the circle from
        // pi/2 that will connect perfectly with the bottom plate.
        // this is a tangent point calculation problem and the equation can
        // be found for example here:
        // http://www.ambrsoft.com/TrigoCalc/Circles2/CirclePoint/CirclePointDistance.htm
        // the y coordinate would be:
        // y = cy + (r^2*py - r*px*sqrt(px^2 + py^2 - r^2) / (px^2 + py^2)
        // where px and py are the coordinates of the point outside the circle
        // cx and cy are the circle center, r is the radius
        // to get the angle we use arcsin function and subtract 90 degrees then
        // flip the sign to get the right input to the round_edge function.
        double r = cfg.edge_radius_mm;
        double cy = 0;
        double cx = 0;
        double px = thickness + wingdist;
        double py = r - fullheight;

        double pxcx = px - cx;
        double pycy = py - cy;
        double b_2 = pxcx*pxcx + pycy*pycy;
        double r_2 = r*r;
        double D = std::sqrt(b_2 - r_2);
        double vy = (r_2*pycy - r*pxcx*D) / b_2;
        double phi = -(std::asin(vy/r) * 180 / PI - 90);

        auto curvedwalls = round_edges(ob,
                                       r,
                                       phi,  // 170 degrees
                                       0,    // z position of the input plane
                                       true,
                                       cfg.throw_on_cancel,
                                       ob, wh);

        pool.merge(curvedwalls);


        auto& thrcl = cfg.throw_on_cancel;

        auto pwalls = walls(ob, inner_base, wh, -fullheight, thrcl);
        pool.merge(pwalls);

        auto cavitywalls = walls(inner_base, middle_base, -wingheight, 0, thrcl);
        pool.merge(cavitywalls);

        Polygons top_triangles, middle_triangles, bottom_triangles;

        triangulate(top_poly, top_triangles);
        triangulate(inner_base, middle_triangles);
        triangulate(inner_base, bottom_triangles);
        auto top_plate = convert(top_triangles, 0, false);
        auto middle_plate = convert(middle_triangles, -mm(wingheight), false);
        auto bottom_plate = convert(bottom_triangles, -mm(fullheight), true);

        pool.merge(top_plate);
        pool.merge(middle_plate);
        pool.merge(bottom_plate);

        out.merge(mesh(pool));
    }

//    double mdist = 2*(1.8*cfg.min_wall_thickness_mm + 4*cfg.edge_radius_mm) +
//                   cfg.max_merge_distance_mm;

//    auto concavehs = concave_hull(ground_layer, mdist, cfg.throw_on_cancel);
//    for(ExPolygon& concaveh : concavehs) {
//        if(concaveh.contour.points.empty()) return;
//        concaveh.holes.clear();

//        const coord_t WALL_THICKNESS = mm(cfg.min_wall_thickness_mm);

//        const coord_t WALL_DISTANCE = mm(2*cfg.edge_radius_mm) +
//                                      coord_t(0.8*WALL_THICKNESS);

//        const coord_t HEIGHT = mm(cfg.min_wall_height_mm);

//        auto outer_base = concaveh;
//        offset(outer_base, WALL_THICKNESS+WALL_DISTANCE);
//        auto inner_base = outer_base;
//        offset(inner_base, -WALL_THICKNESS);
//        inner_base.holes.clear(); outer_base.holes.clear();

//        ExPolygon top_poly;
//        top_poly.contour = outer_base.contour;
//        top_poly.holes.emplace_back(inner_base.contour);
//        auto& tph = top_poly.holes.back().points;
//        std::reverse(tph.begin(), tph.end());

//        Contour3D pool;

//        ExPolygon ob = outer_base; double wh = 0;

//        // now we will calculate the angle or portion of the circle from
//        // pi/2 that will connect perfectly with the bottom plate.
//        // this is a tangent point calculation problem and the equation can
//        // be found for example here:
//        // http://www.ambrsoft.com/TrigoCalc/Circles2/CirclePoint/CirclePointDistance.htm
//        // the y coordinate would be:
//        // y = cy + (r^2*py - r*px*sqrt(px^2 + py^2 - r^2) / (px^2 + py^2)
//        // where px and py are the coordinates of the point outside the circle
//        // cx and cy are the circle center, r is the radius
//        // to get the angle we use arcsin function and subtract 90 degrees then
//        // flip the sign to get the right input to the round_edge function.
//        double r = cfg.edge_radius_mm;
//        double cy = 0;
//        double cx = 0;
//        double px = cfg.min_wall_thickness_mm;
//        double py = r - cfg.min_wall_height_mm;

//        double pxcx = px - cx;
//        double pycy = py - cy;
//        double b_2 = pxcx*pxcx + pycy*pycy;
//        double r_2 = r*r;
//        double D = std::sqrt(b_2 - r_2);
//        double vy = (r_2*pycy - r*pxcx*D) / b_2;
//        double phi = -(std::asin(vy/r) * 180 / PI - 90);

//        auto curvedwalls = round_edges(ob,
//                                       r,
//                                       phi,  // 170 degrees
//                                       0,    // z position of the input plane
//                                       true,
//                                       cfg.throw_on_cancel,
//                                       ob, wh);

//        pool.merge(curvedwalls);

//        ExPolygon ob_contr = ob;
//        ob_contr.holes.clear();

//        auto pwalls = walls(ob_contr, inner_base, wh, -cfg.min_wall_height_mm,
//                            cfg.throw_on_cancel);
//        pool.merge(pwalls);

//        Polygons top_triangles, bottom_triangles;
//        triangulate(top_poly, top_triangles);
//        triangulate(inner_base, bottom_triangles);
//        auto top_plate = convert(top_triangles, 0, false);
//        auto bottom_plate = convert(bottom_triangles, -HEIGHT, true);

//        ob = inner_base; wh = 0;
//        // rounded edge generation for the inner bed
//        curvedwalls = round_edges(ob,
//                                  cfg.edge_radius_mm,
//                                  90,   // 90 degrees
//                                  0,    // z position of the input plane
//                                  false,
//                                  cfg.throw_on_cancel,
//                                  ob, wh);
//        pool.merge(curvedwalls);

//        auto innerbed = inner_bed(ob, cfg.min_wall_height_mm/2 + wh, wh);

//        pool.merge(top_plate);
//        pool.merge(bottom_plate);
//        pool.merge(innerbed);

//        out.merge(mesh(pool));
//    }
}

}
}
