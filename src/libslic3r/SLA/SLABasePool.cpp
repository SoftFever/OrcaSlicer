#include "SLABasePool.hpp"
#include "SLABoilerPlate.hpp"

#include "boost/log/trivial.hpp"
#include "SLABoostAdapter.hpp"
#include "ClipperUtils.hpp"
#include "Tesselate.hpp"
#include "MTUtils.hpp"

// For debugging:
//#include <fstream>
//#include <libnest2d/tools/benchmark.h>
//#include "SVG.hpp"

namespace Slic3r { namespace sla {

/// This function will return a triangulation of a sheet connecting an upper
/// and a lower plate given as input polygons. It will not triangulate the
/// plates themselves only the sheet. The caller has to specify the lower and
/// upper z levels in world coordinates as well as the offset difference
/// between the sheets. If the lower_z_mm is higher than upper_z_mm or the
/// offset difference is negative, the resulting triangle orientation will be
/// reversed.
///
/// IMPORTANT: This is not a universal triangulation algorithm. It assumes
/// that the lower and upper polygons are offsetted versions of the same
/// original polygon. In general, it assumes that one of the polygons is
/// completely inside the other. The offset difference is the reference
/// distance from the inner polygon's perimeter to the outer polygon's
/// perimeter. The real distance will be variable as the clipper offset has
/// different strategies (rounding, etc...). This algorithm should have
/// O(2n + 3m) complexity where n is the number of upper vertices and m is the
/// number of lower vertices.
Contour3D walls(const Polygon& lower, const Polygon& upper,
                double lower_z_mm, double upper_z_mm,
                double offset_difference_mm, ThrowOnCancel thr)
{
    Contour3D ret;

    if(upper.points.size() < 3 || lower.size() < 3) return ret;

    // The concept of the algorithm is relatively simple. It will try to find
    // the closest vertices from the upper and the lower polygon and use those
    // as starting points. Then it will create the triangles sequentially using
    // an edge from the upper polygon and a vertex from the lower or vice versa,
    // depending on the resulting triangle's quality.
    // The quality is measured by a scalar value. So far it looks like it is
    // enough to derive it from the slope of the triangle's two edges connecting
    // the upper and the lower part. A reference slope is calculated from the
    // height and the offset difference.

    // Offset in the index array for the ceiling
    const auto offs = upper.points.size();

    // Shorthand for the vertex arrays
    auto& upoints = upper.points, &lpoints = lower.points;
    auto& rpts = ret.points; auto& ind = ret.indices;

    // If the Z levels are flipped, or the offset difference is negative, we
    // will interpret that as the triangles normals should be inverted.
    bool inverted = upper_z_mm < lower_z_mm || offset_difference_mm < 0;

    // Copy the points into the mesh, convert them from 2D to 3D
    rpts.reserve(upoints.size() + lpoints.size());
    ind.reserve(2 * upoints.size() + 2 * lpoints.size());
    for (auto &p : upoints)
        rpts.emplace_back(unscaled(p.x()), unscaled(p.y()), upper_z_mm);
    for (auto &p : lpoints)
        rpts.emplace_back(unscaled(p.x()), unscaled(p.y()), lower_z_mm);

    // Create pointing indices into vertex arrays. u-upper, l-lower
    size_t uidx = 0, lidx = offs, unextidx = 1, lnextidx = offs + 1;

    // Simple squared distance calculation.
    auto distfn = [](const Vec3d& p1, const Vec3d& p2) {
        auto p = p1 - p2; return p.transpose() * p;
    };

    // We need to find the closest point on lower polygon to the first point on
    // the upper polygon. These will be our starting points.
    double distmin = std::numeric_limits<double>::max();
    for(size_t l = lidx; l < rpts.size(); ++l) {
        thr();
        double d = distfn(rpts[l], rpts[uidx]);
        if(d < distmin) { lidx = l; distmin = d; }
    }

    // Set up lnextidx to be ahead of lidx in cyclic mode
    lnextidx = lidx + 1;
    if(lnextidx == rpts.size()) lnextidx = offs;

    // This will be the flip switch to toggle between upper and lower triangle
    // creation mode
    enum class Proceed {
        UPPER, // A segment from the upper polygon and one vertex from the lower
        LOWER  // A segment from the lower polygon and one vertex from the upper
    } proceed = Proceed::UPPER;

    // Flags to help evaluating loop termination.
    bool ustarted = false, lstarted = false;

    // The variables for the fitness values, one for the actual and one for the
    // previous.
    double current_fit = 0, prev_fit = 0;

    // Every triangle of the wall has two edges connecting the upper plate with
    // the lower plate. From the length of these two edges and the zdiff we
    // can calculate the momentary squared offset distance at a particular
    // position on the wall. The average of the differences from the reference
    // (squared) offset distance will give us the driving fitness value.
    const double offsdiff2 = std::pow(offset_difference_mm, 2);
    const double zdiff2 = std::pow(upper_z_mm - lower_z_mm, 2);

    // Mark the current vertex iterator positions. If the iterators return to
    // the same position, the loop can be terminated.
    size_t uendidx = uidx, lendidx = lidx;

    do { thr();  // check throw if canceled

        prev_fit = current_fit;

        switch(proceed) {   // proceed depending on the current state
        case Proceed::UPPER:
            if(!ustarted || uidx != uendidx) { // there are vertices remaining
                // Get the 3D vertices in order
                const Vec3d& p_up1 = rpts[uidx];
                const Vec3d& p_low = rpts[lidx];
                const Vec3d& p_up2 = rpts[unextidx];

                // Calculate fitness: the average of the two connecting edges
                double a = offsdiff2 - (distfn(p_up1, p_low) - zdiff2);
                double b = offsdiff2 - (distfn(p_up2, p_low) - zdiff2);
                current_fit = (std::abs(a) + std::abs(b)) / 2;

                if(current_fit > prev_fit) { // fit is worse than previously
                    proceed = Proceed::LOWER;
                } else {    // good to go, create the triangle
                    inverted
                        ? ind.emplace_back(int(unextidx), int(lidx), int(uidx))
                        : ind.emplace_back(int(uidx), int(lidx), int(unextidx));

                    // Increment the iterators, rotate if necessary
                    ++uidx; ++unextidx;
                    if(unextidx == offs) unextidx = 0;
                    if(uidx == offs) uidx = 0;

                    ustarted = true;    // mark the movement of the iterators
                    // so that the comparison to uendidx can be made correctly
                }
            } else proceed = Proceed::LOWER;

            break;
        case Proceed::LOWER:
            // Mode with lower segment, upper vertex. Same structure:
            if(!lstarted || lidx != lendidx) {
                const Vec3d& p_low1 = rpts[lidx];
                const Vec3d& p_low2 = rpts[lnextidx];
                const Vec3d& p_up   = rpts[uidx];

                double a = offsdiff2 - (distfn(p_up, p_low1) - zdiff2);
                double b = offsdiff2 - (distfn(p_up, p_low2) - zdiff2);
                current_fit = (std::abs(a) + std::abs(b)) / 2;

                if(current_fit > prev_fit) {
                    proceed = Proceed::UPPER;
                } else {
                    inverted
                        ? ind.emplace_back(int(uidx), int(lnextidx), int(lidx))
                        : ind.emplace_back(int(lidx), int(lnextidx), int(uidx));

                    ++lidx; ++lnextidx;
                    if(lnextidx == rpts.size()) lnextidx = offs;
                    if(lidx == rpts.size()) lidx = offs;

                    lstarted = true;
                }
            } else proceed = Proceed::UPPER;

            break;
        } // end of switch
    } while(!ustarted || !lstarted || uidx != uendidx || lidx != lendidx);

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
    offs.ArcTolerance = scaled<double>(0.01);
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

/// This method will create a rounded edge around a flat polygon in 3d space.
/// 'base_plate' parameter is the target plate.
/// 'radius' is the radius of the edges.
/// 'degrees' is tells how much of a circle should be created as the rounding.
///     It should be in degrees, not radians.
/// 'ceilheight_mm' is the Z coordinate of the flat polygon in 3D space.
/// 'dir' Is the direction of the round edges: inward or outward
/// 'thr' Throws if a cancel signal was received
/// 'last_offset' An auxiliary output variable to save the last offsetted
///     version of 'base_plate'
/// 'last_height' An auxiliary output to save the last z coordinate of the
/// offsetted base_plate. In other words, where the rounded edges end.
Contour3D round_edges(const ExPolygon& base_plate,
                      double radius_mm,
                      double degrees,
                      double ceilheight_mm,
                      bool dir,
                      ThrowOnCancel thr,
                      ExPolygon& last_offset, double& last_height)
{
    auto ob = base_plate;
    auto ob_prev = ob;
    double wh = ceilheight_mm, wh_prev = wh;
    Contour3D curvedwalls;

    int steps = 30;
    double stepx = radius_mm / steps;
    coord_t s = dir? 1 : -1;
    degrees = std::fmod(degrees, 180);

    // we use sin for x distance because we interpret the angle starting from
    // PI/2
    int tos = degrees < 90?
            int(radius_mm*std::cos(degrees * PI / 180 - PI/2) / stepx) : steps;

    for(int i = 1; i <= tos; ++i) {
        thr();

        ob = base_plate;

        double r2 = radius_mm * radius_mm;
        double xx = i*stepx;
        double x2 = xx*xx;
        double stepy = std::sqrt(r2 - x2);

        offset(ob, s * scaled(xx));
        wh = ceilheight_mm - radius_mm + stepy;

        Contour3D pwalls;
        double prev_x = xx - (i - 1) * stepx;
        pwalls = walls(ob.contour, ob_prev.contour, wh, wh_prev, s*prev_x, thr);

        curvedwalls.merge(pwalls);
        ob_prev = ob;
        wh_prev = wh;
    }

    if(degrees > 90) {
        double tox = radius_mm - radius_mm*std::cos(degrees * PI / 180 - PI/2);
        int tos = int(tox / stepx);

        for(int i = 1; i <= tos; ++i) {
            thr();
            ob = base_plate;

            double r2 = radius_mm * radius_mm;
            double xx = radius_mm - i*stepx;
            double x2 = xx*xx;
            double stepy = std::sqrt(r2 - x2);
            offset(ob, s * scaled(xx));
            wh = ceilheight_mm - radius_mm - stepy;

            Contour3D pwalls;
            double prev_x = xx - radius_mm + (i - 1)*stepx;
            pwalls =
                walls(ob_prev.contour, ob.contour, wh_prev, wh, s*prev_x, thr);

            curvedwalls.merge(pwalls);
            ob_prev = ob;
            wh_prev = wh;
        }
    }

    last_offset = std::move(ob);
    last_height = wh;

    return curvedwalls;
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
        double max_dist = scaled<double>(max_dist_mm);

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

        Point d(scaled(nx), scaled(ny));
        ctour.emplace_back(c + Point( -y(d),  x(d) ));
        ctour.emplace_back(c + Point(  y(d), -x(d) ));
        offset(r, scaled(1.));

        return r;
    });

    // This is unavoidable...
    punion = unify(punion);

    return punion;
}

void base_plate(const TriangleMesh &mesh, ExPolygons &output, float h,
                float layerh, ThrowOnCancel thrfn)
{
    TriangleMesh m = mesh;
    m.require_shared_vertices(); // TriangleMeshSlicer needs this
    TriangleMeshSlicer slicer(&m);

    auto bb = mesh.bounding_box();
    float gnd = float(bb.min(Z));
    std::vector<float> heights = {float(bb.min(Z))};
    for(float hi = gnd + layerh; hi <= gnd + h; hi += layerh)
        heights.emplace_back(hi);

    std::vector<ExPolygons> out; out.reserve(size_t(std::ceil(h/layerh)));
    slicer.slice(heights, 0.f, &out, thrfn);

    size_t count = 0; for(auto& o : out) count += o.size();

    // Now we have to unify all slice layers which can be an expensive operation
    // so we will try to simplify the polygons
    ExPolygons tmp; tmp.reserve(count);
    for(ExPolygons& o : out)
        for(ExPolygon& e : o) {
            auto&& exss = e.simplify(scaled<double>(0.1));
            for(ExPolygon& ep : exss) tmp.emplace_back(std::move(ep));
        }

    ExPolygons utmp = unify(tmp);

    for(auto& o : utmp) {
        auto&& smp = o.simplify(scaled<double>(0.1));
        output.insert(output.end(), smp.begin(), smp.end());
    }
}

Contour3D create_base_pool(const ExPolygons &ground_layer, 
                           const PoolConfig& cfg = PoolConfig()) 
{
    // for debugging:
    // Benchmark bench;
    // bench.start();

    double mergedist = 2*(1.8*cfg.min_wall_thickness_mm + 4*cfg.edge_radius_mm)+
                       cfg.max_merge_distance_mm;

    // Here we get the base polygon from which the pad has to be generated.
    // We create an artificial concave hull from this polygon and that will
    // serve as the bottom plate of the pad. We will offset this concave hull
    // and then offset back the result with clipper with rounding edges ON. This
    // trick will create a nice rounded pad shape.
    ExPolygons concavehs = concave_hull(ground_layer, mergedist, cfg.throw_on_cancel);

    const double thickness      = cfg.min_wall_thickness_mm;
    const double wingheight     = cfg.min_wall_height_mm;
    const double fullheight     = wingheight + thickness;
    const double slope          = cfg.wall_slope;
    const double wingdist       = wingheight / std::tan(slope);
    const double bottom_offs    = (thickness + wingheight) / std::tan(slope);

    // scaled values
    const coord_t s_thickness   = scaled(thickness);
    const coord_t s_eradius     = scaled(cfg.edge_radius_mm);
    const coord_t s_safety_dist = 2*s_eradius + coord_t(0.8*s_thickness);
    const coord_t s_wingdist    = scaled(wingdist);
    const coord_t s_bottom_offs = scaled(bottom_offs);

    auto& thrcl = cfg.throw_on_cancel;

    Contour3D pool;

    for(ExPolygon& concaveh : concavehs) {
        if(concaveh.contour.points.empty()) return pool;

        // Get rid of any holes in the concave hull output.
        concaveh.holes.clear();

        // Here lies the trick that does the smoothing only with clipper offset
        // calls. The offset is configured to round edges. Inner edges will
        // be rounded because we offset twice: ones to get the outer (top) plate
        // and again to get the inner (bottom) plate
        auto outer_base = concaveh;
        outer_base.holes.clear();
        offset(outer_base, s_safety_dist + s_wingdist + s_thickness);

        ExPolygon bottom_poly = outer_base;
        bottom_poly.holes.clear();
        offset(bottom_poly, -s_bottom_offs);

        // Punching a hole in the top plate for the cavity
        ExPolygon top_poly;
        ExPolygon middle_base;
        ExPolygon inner_base;
        top_poly.contour = outer_base.contour;

        if(wingheight > 0) {
            inner_base = outer_base;
            offset(inner_base, -(s_thickness + s_wingdist + s_eradius));

            middle_base = outer_base;
            offset(middle_base, -s_thickness);
            top_poly.holes.emplace_back(middle_base.contour);
            auto& tph = top_poly.holes.back().points;
            std::reverse(tph.begin(), tph.end());
        }

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
        // We place the circle center to (0, 0) in the calculation the make
        // things easier.
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


        // Generate the smoothed edge geometry
        if(s_eradius > 0) pool.merge(round_edges(ob,
                               r,
                               phi,
                               0,    // z position of the input plane
                               true,
                               thrcl,
                               ob, wh));

        // Now that we have the rounded edge connecting the top plate with
        // the outer side walls, we can generate and merge the sidewall geometry
        pool.merge(walls(ob.contour, bottom_poly.contour, wh, -fullheight,
                         bottom_offs, thrcl));

        if(wingheight > 0) {
            // Generate the smoothed edge geometry
            wh = 0;
            if(s_eradius) pool.merge(round_edges(middle_base,
                                   r,
                                   phi - 90, // from tangent lines
                                   0,  // z position of the input plane
                                   false,
                                   thrcl,
                                   ob, wh));

            // Next is the cavity walls connecting to the top plate's
            // artificially created hole.
            pool.merge(walls(inner_base.contour, ob.contour, -wingheight,
                             wh, -wingdist, thrcl));
        }

        // Now we need to triangulate the top and bottom plates as well as the
        // cavity bottom plate which is the same as the bottom plate but it is
        // elevated by the thickness.
        pool.merge(triangulate_expolygon_3d(top_poly));
        pool.merge(triangulate_expolygon_3d(bottom_poly, -fullheight, true));

        if(wingheight > 0)
            pool.merge(triangulate_expolygon_3d(inner_base, -wingheight));

    }
    
    return pool;
}

void create_base_pool(const ExPolygons &ground_layer, TriangleMesh& out,
                      const PoolConfig& cfg)
{
    

    // For debugging:
    // bench.stop();
    // std::cout << "Pad creation time: " << bench.getElapsedSec() << std::endl;
    // std::fstream fout("pad_debug.obj", std::fstream::out);
    // if(fout.good()) pool.to_obj(fout);

    out.merge(mesh(create_base_pool(ground_layer, cfg)));
}

}
}
