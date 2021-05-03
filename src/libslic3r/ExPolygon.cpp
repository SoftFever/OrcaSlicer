#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "Exception.hpp"
#include "Geometry.hpp"
#include "Polygon.hpp"
#include "Line.hpp"
#include "ClipperUtils.hpp"
#include "SVG.hpp"
#include <algorithm>
#include <cassert>
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
    return to_polygons(*this);
}

ExPolygon::operator Polylines() const
{
    return to_polylines(*this);
}

void ExPolygon::scale(double factor)
{
    contour.scale(factor);
    for (Polygon &hole : holes)
        hole.scale(factor);
}

void ExPolygon::translate(const Point &p)
{
    contour.translate(p);
    for (Polygon &hole : holes)
        hole.translate(p);
}

void ExPolygon::rotate(double angle)
{
    contour.rotate(angle);
    for (Polygon &hole : holes)
        hole.rotate(angle);
}

void ExPolygon::rotate(double angle, const Point &center)
{
    contour.rotate(angle, center);
    for (Polygon &hole : holes)
        hole.rotate(angle, center);
}

double ExPolygon::area() const
{
    double a = this->contour.area();
    for (const Polygon &hole : holes)
        a -= - hole.area();  // holes have negative area
    return a;
}

bool ExPolygon::is_valid() const
{
    if (!this->contour.is_valid() || !this->contour.is_counter_clockwise()) return false;
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        if (!(*it).is_valid() || (*it).is_counter_clockwise()) return false;
    }
    return true;
}

void ExPolygon::douglas_peucker(double tolerance)
{
    this->contour.douglas_peucker(tolerance);
    for (Polygon &poly : this->holes)
        poly.douglas_peucker(tolerance);
}

bool ExPolygon::contains(const Line &line) const
{
    return this->contains(Polyline(line.a, line.b));
}

bool ExPolygon::contains(const Polyline &polyline) const
{
    return diff_pl((Polylines)polyline, *this).empty();
}

bool ExPolygon::contains(const Polylines &polylines) const
{
    #if 0
    BoundingBox bbox = get_extents(polylines);
    bbox.merge(get_extents(*this));
    SVG svg(debug_out_path("ExPolygon_contains.svg"), bbox);
    svg.draw(*this);
    svg.draw_outline(*this);
    svg.draw(polylines, "blue");
    #endif
    Polylines pl_out = diff_pl(polylines, *this);
    #if 0
    svg.draw(pl_out, "red");
    #endif
    return pl_out.empty();
}

bool ExPolygon::contains(const Point &point) const
{
    if (!this->contour.contains(point)) return false;
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        if (it->contains(point)) return false;
    }
    return true;
}

// inclusive version of contains() that also checks whether point is on boundaries
bool ExPolygon::contains_b(const Point &point) const
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

bool ExPolygon::overlaps(const ExPolygon &other) const
{
    #if 0
    BoundingBox bbox = get_extents(other);
    bbox.merge(get_extents(*this));
    static int iRun = 0;
    SVG svg(debug_out_path("ExPolygon_overlaps-%d.svg", iRun ++), bbox);
    svg.draw(*this);
    svg.draw_outline(*this);
    svg.draw_outline(other, "blue");
    #endif
    Polylines pl_out = intersection_pl((Polylines)other, *this);
    #if 0
    svg.draw(pl_out, "red");
    #endif
    if (! pl_out.empty())
        return true; 
    //FIXME ExPolygon::overlaps() shall be commutative, it is not!
    return ! other.contour.points.empty() && this->contains_b(other.contour.points.front());
}

void ExPolygon::simplify_p(double tolerance, Polygons* polygons) const
{
    Polygons pp = this->simplify_p(tolerance);
    polygons->insert(polygons->end(), pp.begin(), pp.end());
}

Polygons ExPolygon::simplify_p(double tolerance) const
{
    Polygons pp;
    pp.reserve(this->holes.size() + 1);
    // contour
    {
        Polygon p = this->contour;
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.emplace_back(std::move(p));
    }
    // holes
    for (Polygon p : this->holes) {
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.emplace_back(std::move(p));
    }
    return simplify_polygons(pp);
}

ExPolygons ExPolygon::simplify(double tolerance) const
{
    return union_ex(this->simplify_p(tolerance));
}

void ExPolygon::simplify(double tolerance, ExPolygons* expolygons) const
{
    append(*expolygons, this->simplify(tolerance));
}

void
ExPolygon::medial_axis(double max_width, double min_width, ThickPolylines* polylines) const
{
    // init helper object
    Slic3r::Geometry::MedialAxis ma(max_width, min_width, this);
    ma.lines = this->lines();
    
    // compute the Voronoi diagram and extract medial axis polylines
    ThickPolylines pp;
    ma.build(&pp);
    
    /*
    SVG svg("medial_axis.svg");
    svg.draw(*this);
    svg.draw(pp);
    svg.Close();
    */
    
    /* Find the maximum width returned; we're going to use this for validating and 
       filtering the output segments. */
    double max_w = 0;
    for (ThickPolylines::const_iterator it = pp.begin(); it != pp.end(); ++it)
        max_w = fmaxf(max_w, *std::max_element(it->width.begin(), it->width.end()));
    
    /* Loop through all returned polylines in order to extend their endpoints to the 
       expolygon boundaries */
    bool removed = false;
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];
        
        // extend initial and final segments of each polyline if they're actual endpoints
        /* We assign new endpoints to temporary variables because in case of a single-line
           polyline, after we extend the start point it will be caught by the intersection()
           call, so we keep the inner point until we perform the second intersection() as well */
        Point new_front = polyline.points.front();
        Point new_back  = polyline.points.back();
        if (polyline.endpoints.first && !this->has_boundary_point(new_front)) {
            Vec2d p1 = polyline.points.front().cast<double>();
            Vec2d p2 = polyline.points[1].cast<double>();
            // prevent the line from touching on the other side, otherwise intersection() might return that solution
            if (polyline.points.size() == 2)
                p2 = (p1 + p2) * 0.5;
            // Extend the start of the segment.
            p1 -= (p2 - p1).normalized() * max_width;
            this->contour.intersection(Line(p1.cast<coord_t>(), p2.cast<coord_t>()), &new_front);
        }
        if (polyline.endpoints.second && !this->has_boundary_point(new_back)) {
            Vec2d p1 = (polyline.points.end() - 2)->cast<double>();
            Vec2d p2 = polyline.points.back().cast<double>();
            // prevent the line from touching on the other side, otherwise intersection() might return that solution
            if (polyline.points.size() == 2)
                p1 = (p1 + p2) * 0.5;
            // Extend the start of the segment.
            p2 += (p2 - p1).normalized() * max_width;
            this->contour.intersection(Line(p1.cast<coord_t>(), p2.cast<coord_t>()), &new_back);
        }
        polyline.points.front() = new_front;
        polyline.points.back()  = new_back;
        
        /*  remove too short polylines
            (we can't do this check before endpoints extension and clipping because we don't
            know how long will the endpoints be extended since it depends on polygon thickness
            which is variable - extension will be <= max_width/2 on each side)  */
        if ((polyline.endpoints.first || polyline.endpoints.second)
            && polyline.length() < max_w*2) {
            pp.erase(pp.begin() + i);
            --i;
            removed = true;
            continue;
        }
    }
    
    /*  If we removed any short polylines we now try to connect consecutive polylines
        in order to allow loop detection. Note that this algorithm is greedier than 
        MedialAxis::process_edge_neighbors() as it will connect random pairs of 
        polylines even when more than two start from the same point. This has no 
        drawbacks since we optimize later using nearest-neighbor which would do the 
        same, but should we use a more sophisticated optimization algorithm we should
        not connect polylines when more than two meet.  */
    if (removed) {
        for (size_t i = 0; i < pp.size(); ++i) {
            ThickPolyline& polyline = pp[i];
            if (polyline.endpoints.first && polyline.endpoints.second) continue; // optimization
            
            // find another polyline starting here
            for (size_t j = i+1; j < pp.size(); ++j) {
                ThickPolyline& other = pp[j];
                if (polyline.last_point() == other.last_point()) {
                    other.reverse();
                } else if (polyline.first_point() == other.last_point()) {
                    polyline.reverse();
                    other.reverse();
                } else if (polyline.first_point() == other.first_point()) {
                    polyline.reverse();
                } else if (polyline.last_point() != other.first_point()) {
                    continue;
                }
                
                polyline.points.insert(polyline.points.end(), other.points.begin() + 1, other.points.end());
                polyline.width.insert(polyline.width.end(), other.width.begin(), other.width.end());
                polyline.endpoints.second = other.endpoints.second;
                assert(polyline.width.size() == polyline.points.size()*2 - 2);
                
                pp.erase(pp.begin() + j);
                j = i;  // restart search from i+1
            }
        }
    }
    
    polylines->insert(polylines->end(), pp.begin(), pp.end());
}

void
ExPolygon::medial_axis(double max_width, double min_width, Polylines* polylines) const
{
    ThickPolylines tp;
    this->medial_axis(max_width, min_width, &tp);
    polylines->insert(polylines->end(), tp.begin(), tp.end());
}

Lines ExPolygon::lines() const
{
    Lines lines = this->contour.lines();
    for (Polygons::const_iterator h = this->holes.begin(); h != this->holes.end(); ++h) {
        Lines hole_lines = h->lines();
        lines.insert(lines.end(), hole_lines.begin(), hole_lines.end());
    }
    return lines;
}

BoundingBox get_extents(const ExPolygon &expolygon)
{
    return get_extents(expolygon.contour);
}

BoundingBox get_extents(const ExPolygons &expolygons)
{
    BoundingBox bbox;
    if (! expolygons.empty()) {
        for (size_t i = 0; i < expolygons.size(); ++ i)
			if (! expolygons[i].contour.points.empty())
				bbox.merge(get_extents(expolygons[i]));
    }
    return bbox;
}

BoundingBox get_extents_rotated(const ExPolygon &expolygon, double angle)
{
    return get_extents_rotated(expolygon.contour, angle);
}

BoundingBox get_extents_rotated(const ExPolygons &expolygons, double angle)
{
    BoundingBox bbox;
    if (! expolygons.empty()) {
        bbox = get_extents_rotated(expolygons.front().contour, angle);
        for (size_t i = 1; i < expolygons.size(); ++ i)
            bbox.merge(get_extents_rotated(expolygons[i].contour, angle));
    }
    return bbox;
}

extern std::vector<BoundingBox> get_extents_vector(const ExPolygons &polygons)
{
    std::vector<BoundingBox> out;
    out.reserve(polygons.size());
    for (ExPolygons::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        out.push_back(get_extents(*it));
    return out;
}

bool remove_sticks(ExPolygon &poly)
{
    return remove_sticks(poly.contour) || remove_sticks(poly.holes);
}

bool remove_small_and_small_holes(ExPolygons &expolygons, double min_area)
{
    bool   modified = false;
    size_t free_idx = 0;
    for (size_t expoly_idx = 0; expoly_idx < expolygons.size(); ++expoly_idx) {
        if (std::abs(expolygons[expoly_idx].area()) >= min_area) {
            // Expolygon is big enough, so also check all its holes
            modified |= remove_small(expolygons[expoly_idx].holes, min_area);
            if (free_idx < expoly_idx) {
                std::swap(expolygons[expoly_idx].contour, expolygons[free_idx].contour);
                std::swap(expolygons[expoly_idx].holes, expolygons[free_idx].holes);
            }
            ++free_idx;
        } else
            modified = true;
    }
    if (free_idx < expolygons.size())
        expolygons.erase(expolygons.begin() + free_idx, expolygons.end());
    return modified;
}

void keep_largest_contour_only(ExPolygons &polygons)
{
	if (polygons.size() > 1) {
	    double     max_area = 0.;
	    ExPolygon* max_area_polygon = nullptr;
	    for (ExPolygon& p : polygons) {
	        double a = p.contour.area();
	        if (a > max_area) {
	            max_area         = a;
	            max_area_polygon = &p;
	        }
	    }
	    assert(max_area_polygon != nullptr);
	    ExPolygon p(std::move(*max_area_polygon));
	    polygons.clear();
	    polygons.emplace_back(std::move(p));
	}
}

} // namespace Slic3r
