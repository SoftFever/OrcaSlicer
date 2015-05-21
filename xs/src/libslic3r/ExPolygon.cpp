#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "Geometry.hpp"
#include "Polygon.hpp"
#include "Line.hpp"
#include "ClipperUtils.hpp"
#include "polypartition.h"
#include "poly2tri/poly2tri.h"

#include <algorithm>
#include <list>

namespace Slic3r {

ExPolygon::operator Points() const
{
    Points points;
    Polygons pp = *this;
    for (Polygons::const_iterator poly = pp.begin(); poly != pp.end(); ++poly) {
        for (Points::const_iterator point = poly->points.begin(); point != poly->points.end(); ++point)
            points.push_back(*point);
    }
    return points;
}

ExPolygon::operator Polygons() const
{
    Polygons polygons;
    polygons.reserve(this->holes.size() + 1);
    polygons.push_back(this->contour);
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        polygons.push_back(*it);
    }
    return polygons;
}

void
ExPolygon::scale(double factor)
{
    contour.scale(factor);
    for (Polygons::iterator it = holes.begin(); it != holes.end(); ++it) {
        (*it).scale(factor);
    }
}

void
ExPolygon::translate(double x, double y)
{
    contour.translate(x, y);
    for (Polygons::iterator it = holes.begin(); it != holes.end(); ++it) {
        (*it).translate(x, y);
    }
}

void
ExPolygon::rotate(double angle, const Point &center)
{
    contour.rotate(angle, center);
    for (Polygons::iterator it = holes.begin(); it != holes.end(); ++it) {
        (*it).rotate(angle, center);
    }
}

double
ExPolygon::area() const
{
    double a = this->contour.area();
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        a -= -(*it).area();  // holes have negative area
    }
    return a;
}

bool
ExPolygon::is_valid() const
{
    if (!this->contour.is_valid() || !this->contour.is_counter_clockwise()) return false;
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        if (!(*it).is_valid() || (*it).is_counter_clockwise()) return false;
    }
    return true;
}

bool
ExPolygon::contains(const Line &line) const
{
    return this->contains((Polyline)line);
}

bool
ExPolygon::contains(const Polyline &polyline) const
{
    Polylines pl_out;
    diff((Polylines)polyline, *this, &pl_out);
    return pl_out.empty();
}

bool
ExPolygon::contains(const Point &point) const
{
    if (!this->contour.contains(point)) return false;
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        if (it->contains(point)) return false;
    }
    return true;
}

// inclusive version of contains() that also checks whether point is on boundaries
bool
ExPolygon::contains_b(const Point &point) const
{
    return this->contains(point) || this->has_boundary_point(point);
}

bool
ExPolygon::has_boundary_point(const Point &point) const
{
    if (this->contour.has_boundary_point(point)) return true;
    for (Polygons::const_iterator h = this->holes.begin(); h != this->holes.end(); ++h) {
        if (h->has_boundary_point(point)) return true;
    }
    return false;
}

Polygons
ExPolygon::simplify_p(double tolerance) const
{
    Polygons pp;
    pp.reserve(this->holes.size() + 1);
    
    // contour
    {
        Polygon p = this->contour;
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.push_back(p);
    }
    
    // holes
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        Polygon p = *it;
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.push_back(p);
    }
    simplify_polygons(pp, &pp);
    return pp;
}

ExPolygons
ExPolygon::simplify(double tolerance) const
{
    Polygons pp = this->simplify_p(tolerance);
    ExPolygons expp;
    union_(pp, &expp);
    return expp;
}

void
ExPolygon::simplify(double tolerance, ExPolygons &expolygons) const
{
    ExPolygons ep = this->simplify(tolerance);
    expolygons.reserve(expolygons.size() + ep.size());
    expolygons.insert(expolygons.end(), ep.begin(), ep.end());
}

void
ExPolygon::medial_axis(double max_width, double min_width, Polylines* polylines) const
{
    // init helper object
    Slic3r::Geometry::MedialAxis ma(max_width, min_width);
    
    // populate list of segments for the Voronoi diagram
    ma.lines = this->contour.lines();
    for (Polygons::const_iterator hole = this->holes.begin(); hole != this->holes.end(); ++hole) {
        Lines lines = hole->lines();
        ma.lines.insert(ma.lines.end(), lines.begin(), lines.end());
    }
    
    // compute the Voronoi diagram
    ma.build(polylines);
    
    // clip segments to our expolygon area
    // (do this before extending endpoints as external segments coule be extended into
    // expolygon, this leaving wrong things inside)
    intersection(*polylines, *this, polylines);
    
    // extend initial and final segments of each polyline (they will be clipped)
    // unless they represent closed loops
    for (Polylines::iterator polyline = polylines->begin(); polyline != polylines->end(); ++polyline) {
        if (polyline->points.front().distance_to(polyline->points.back()) < min_width) continue;
        // TODO: we should *not* extend endpoints where other polylines start/end
        // (such as T joints, which are returned as three polylines by MedialAxis)
        polyline->extend_start(max_width);
        polyline->extend_end(max_width);
    }
    
    // clip again after extending endpoints to prevent them from exceeding the expolygon boundaries
    intersection(*polylines, *this, polylines);
    
    // remove too short polylines
    // (we can't do this check before endpoints extension and clipping because we don't
    // know how long will the endpoints be extended since it depends on polygon thickness
    // which is variable - extension will be <= max_width/2 on each side)
    for (size_t i = 0; i < polylines->size(); ++i) {
        if ((*polylines)[i].length() < max_width) {
            polylines->erase(polylines->begin() + i);
            --i;
        }
    }
}

void
ExPolygon::get_trapezoids(Polygons* polygons) const
{
    ExPolygons expp;
    expp.push_back(*this);
    boost::polygon::get_trapezoids(*polygons, expp);
}

void
ExPolygon::get_trapezoids(Polygons* polygons, double angle) const
{
    ExPolygon clone = *this;
    clone.rotate(PI/2 - angle, Point(0,0));
    clone.get_trapezoids(polygons);
    for (Polygons::iterator polygon = polygons->begin(); polygon != polygons->end(); ++polygon)
        polygon->rotate(-(PI/2 - angle), Point(0,0));
}

// This algorithm may return more trapezoids than necessary
// (i.e. it may break a single trapezoid in several because
// other parts of the object have x coordinates in the middle)
void
ExPolygon::get_trapezoids2(Polygons* polygons) const
{
    // get all points of this ExPolygon
    Points pp = *this;
    
    // build our bounding box
    BoundingBox bb(pp);
    
    // get all x coordinates
    std::vector<coord_t> xx;
    xx.reserve(pp.size());
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p)
        xx.push_back(p->x);
    std::sort(xx.begin(), xx.end());
    
    // find trapezoids by looping from first to next-to-last coordinate
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end()-1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x == next_x) continue;
        
        // build rectangle
        Polygon poly;
        poly.points.resize(4);
        poly[0].x = *x;
        poly[0].y = bb.min.y;
        poly[1].x = next_x;
        poly[1].y = bb.min.y;
        poly[2].x = next_x;
        poly[2].y = bb.max.y;
        poly[3].x = *x;
        poly[3].y = bb.max.y;
        
        // intersect with this expolygon
        Polygons trapezoids;
        intersection<Polygons,Polygons>(poly, *this, &trapezoids);
        
        // append results to return value
        polygons->insert(polygons->end(), trapezoids.begin(), trapezoids.end());
    }
}

void
ExPolygon::get_trapezoids2(Polygons* polygons, double angle) const
{
    ExPolygon clone = *this;
    clone.rotate(PI/2 - angle, Point(0,0));
    clone.get_trapezoids2(polygons);
    for (Polygons::iterator polygon = polygons->begin(); polygon != polygons->end(); ++polygon)
        polygon->rotate(-(PI/2 - angle), Point(0,0));
}

// While this triangulates successfully, it's NOT a constrained triangulation
// as it will create more vertices on the boundaries than the ones supplied.
void
ExPolygon::triangulate(Polygons* polygons) const
{
    // first make trapezoids
    Polygons trapezoids;
    this->get_trapezoids2(&trapezoids);
    
    // then triangulate each trapezoid
    for (Polygons::iterator polygon = trapezoids.begin(); polygon != trapezoids.end(); ++polygon)
        polygon->triangulate_convex(polygons);
}

void
ExPolygon::triangulate_pp(Polygons* polygons) const
{
    // convert polygons
    std::list<TPPLPoly> input;
    
    Polygons pp = *this;
    simplify_polygons(pp, &pp, true);
    ExPolygons expp;
    union_(pp, &expp);
    
    for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex) {
        // contour
        {
            TPPLPoly p;
            p.Init(ex->contour.points.size());
            //printf("%zu\n0\n", ex->contour.points.size());
            for (Points::const_iterator point = ex->contour.points.begin(); point != ex->contour.points.end(); ++point) {
                p[ point-ex->contour.points.begin() ].x = point->x;
                p[ point-ex->contour.points.begin() ].y = point->y;
                //printf("%ld %ld\n", point->x, point->y);
            }
            p.SetHole(false);
            input.push_back(p);
        }
    
        // holes
        for (Polygons::const_iterator hole = ex->holes.begin(); hole != ex->holes.end(); ++hole) {
            TPPLPoly p;
            p.Init(hole->points.size());
            //printf("%zu\n1\n", hole->points.size());
            for (Points::const_iterator point = hole->points.begin(); point != hole->points.end(); ++point) {
                p[ point-hole->points.begin() ].x = point->x;
                p[ point-hole->points.begin() ].y = point->y;
                //printf("%ld %ld\n", point->x, point->y);
            }
            p.SetHole(true);
            input.push_back(p);
        }
    }
    
    // perform triangulation
    std::list<TPPLPoly> output;
    int res = TPPLPartition().Triangulate_MONO(&input, &output);
    if (res != 1) CONFESS("Triangulation failed");
    
    // convert output polygons
    for (std::list<TPPLPoly>::iterator poly = output.begin(); poly != output.end(); ++poly) {
        long num_points = poly->GetNumPoints();
        Polygon p;
        p.points.resize(num_points);
        for (long i = 0; i < num_points; ++i) {
            p.points[i].x = (*poly)[i].x;
            p.points[i].y = (*poly)[i].y;
        }
        polygons->push_back(p);
    }
}

void
ExPolygon::triangulate_p2t(Polygons* polygons) const
{
    ExPolygons expp;
    simplify_polygons(*this, &expp, true);
    
    for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex) {
        p2t::CDT* cdt;
        
        // TODO: prevent duplicate points
        
        // contour
        {
            std::vector<p2t::Point*> points;
            for (Points::const_iterator point = ex->contour.points.begin(); point != ex->contour.points.end(); ++point) {
                points.push_back(new p2t::Point(point->x, point->y));
            }
            cdt = new p2t::CDT(points);
        }
    
        // holes
        for (Polygons::const_iterator hole = ex->holes.begin(); hole != ex->holes.end(); ++hole) {
            std::vector<p2t::Point*> points;
            for (Points::const_iterator point = hole->points.begin(); point != hole->points.end(); ++point) {
                points.push_back(new p2t::Point(point->x, point->y));
            }
            cdt->AddHole(points);
        }
        
        // perform triangulation
        cdt->Triangulate();
        std::vector<p2t::Triangle*> triangles = cdt->GetTriangles();
        
        for (std::vector<p2t::Triangle*>::const_iterator triangle = triangles.begin(); triangle != triangles.end(); ++triangle) {
            Polygon p;
            for (int i = 0; i <= 2; ++i) {
                p2t::Point* point = (*triangle)->GetPoint(i);
                p.points.push_back(Point(point->x, point->y));
            }
            polygons->push_back(p);
        }
    }
}

Lines
ExPolygon::lines() const
{
    Lines lines = this->contour.lines();
    for (Polygons::const_iterator h = this->holes.begin(); h != this->holes.end(); ++h) {
        Lines hole_lines = h->lines();
        lines.insert(lines.end(), hole_lines.begin(), hole_lines.end());
    }
    return lines;
}

#ifdef SLIC3RXS

REGISTER_CLASS(ExPolygon, "ExPolygon");

SV*
ExPolygon::to_AV() {
    const unsigned int num_holes = this->holes.size();
    AV* av = newAV();
    av_extend(av, num_holes);  // -1 +1
    
    av_store(av, 0, perl_to_SV_ref(this->contour));
    
    for (unsigned int i = 0; i < num_holes; i++) {
        av_store(av, i+1, perl_to_SV_ref(this->holes[i]));
    }
    return newRV_noinc((SV*)av);
}

SV*
ExPolygon::to_SV_pureperl() const
{
    const unsigned int num_holes = this->holes.size();
    AV* av = newAV();
    av_extend(av, num_holes);  // -1 +1
    av_store(av, 0, this->contour.to_SV_pureperl());
    for (unsigned int i = 0; i < num_holes; i++) {
        av_store(av, i+1, this->holes[i].to_SV_pureperl());
    }
    return newRV_noinc((SV*)av);
}

void
ExPolygon::from_SV(SV* expoly_sv)
{
    AV* expoly_av = (AV*)SvRV(expoly_sv);
    const unsigned int num_polygons = av_len(expoly_av)+1;
    this->holes.resize(num_polygons-1);
    
    SV** polygon_sv = av_fetch(expoly_av, 0, 0);
    this->contour.from_SV(*polygon_sv);
    for (unsigned int i = 0; i < num_polygons-1; i++) {
        polygon_sv = av_fetch(expoly_av, i+1, 0);
        this->holes[i].from_SV(*polygon_sv);
    }
}

void
ExPolygon::from_SV_check(SV* expoly_sv)
{
    if (sv_isobject(expoly_sv) && (SvTYPE(SvRV(expoly_sv)) == SVt_PVMG)) {
        if (!sv_isa(expoly_sv, perl_class_name(this)) && !sv_isa(expoly_sv, perl_class_name_ref(this)))
          CONFESS("Not a valid %s object", perl_class_name(this));
        // a XS ExPolygon was supplied
        *this = *(ExPolygon *)SvIV((SV*)SvRV( expoly_sv ));
    } else {
        // a Perl arrayref was supplied
        this->from_SV(expoly_sv);
    }
}
#endif

}
