#include <stdlib.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include <boost/static_assert.hpp>

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Geometry.hpp"
#include "../Surface.hpp"
#include "../Int128.hpp"

#include "FillRectilinear3.hpp"

 #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #undef NDEBUG
    #define DEBUG
    #define _DEBUG
    #include "SVG.hpp"
#endif

#include <cassert>

namespace Slic3r {

namespace FillRectilinear3_Internal {

// A container maintaining the source expolygon with its inner offsetted polygon.
// The source expolygon is offsetted twice: 
// 1) A tiny offset is used to get a contour, to which the open hatching lines will be extended.
// 2) A larger offset is used to get a contor, along which the individual hatching lines will be connected.
struct ExPolygonWithOffset
{
public:
    ExPolygonWithOffset(
        const ExPolygon &expolygon,
        float aoffset1,
        float aoffset2)
    {
        // Copy and rotate the source polygons.
        polygons_src = expolygon;

        double mitterLimit = 3.;
        // for the infill pattern, don't cut the corners.
        // default miterLimt = 3
        //double mitterLimit = 10.;
        assert(aoffset1 < 0);
        assert(aoffset2 < 0);
        assert(aoffset2 < aoffset1);
//        bool sticks_removed = remove_sticks(polygons_src);
//        if (sticks_removed) printf("Sticks removed!\n");
        polygons_outer = offset(polygons_src, aoffset1,
            ClipperLib::jtMiter,
            mitterLimit);
        polygons_inner = offset(polygons_outer, aoffset2 - aoffset1,
            ClipperLib::jtMiter,
            mitterLimit);
        // Filter out contours with zero area or small area, contours with 2 points only.
        const double min_area_threshold = 0.01 * aoffset2 * aoffset2;
        remove_small(polygons_outer, min_area_threshold);
        remove_small(polygons_inner, min_area_threshold);
        remove_sticks(polygons_outer);
        remove_sticks(polygons_inner);
        n_contours_outer = polygons_outer.size();
        n_contours_inner = polygons_inner.size();
        n_contours = n_contours_outer + n_contours_inner;
        polygons_ccw.assign(n_contours, false);
        for (size_t i = 0; i < n_contours; ++ i) {
            contour(i).remove_duplicate_points();
            assert(! contour(i).has_duplicate_points());
            polygons_ccw[i] = Slic3r::Geometry::is_ccw(contour(i));
        }
    }

    // Any contour with offset1
    bool             is_contour_outer(size_t idx) const { return idx < n_contours_outer; }
    // Any contour with offset2
    bool             is_contour_inner(size_t idx) const { return idx >= n_contours_outer; }

    const Polygon&   contour(size_t idx) const 
        { return is_contour_outer(idx) ? polygons_outer[idx] : polygons_inner[idx - n_contours_outer]; }

    Polygon&         contour(size_t idx)
        { return is_contour_outer(idx) ? polygons_outer[idx] : polygons_inner[idx - n_contours_outer]; }

    bool             is_contour_ccw(size_t idx) const { return polygons_ccw[idx] != 0; }

    BoundingBox      bounding_box_src() const 
        { return get_extents(polygons_src); }
    BoundingBox      bounding_box_outer() const 
        { return get_extents(polygons_outer); }
    BoundingBox      bounding_box_inner() const 
        { return get_extents(polygons_inner); }

#ifdef SLIC3R_DEBUG
    void export_to_svg(Slic3r::SVG &svg) const {
        svg.draw_outline(polygons_src,   "black");
        svg.draw_outline(polygons_outer, "green");
        svg.draw_outline(polygons_inner, "brown");
    }
#endif /* SLIC3R_DEBUG */

    ExPolygon        polygons_src;
    Polygons         polygons_outer;
    Polygons         polygons_inner;

    size_t           n_contours_outer;
    size_t           n_contours_inner;
    size_t           n_contours;

protected:
    // For each polygon of polygons_inner, remember its orientation.
    std::vector<unsigned char> polygons_ccw;
};

class SegmentedIntersectionLine;

// Intersection point of a vertical line with a polygon segment.
class SegmentIntersection
{
public:
    SegmentIntersection() :
        line(nullptr),
        expoly_with_offset(nullptr),
        iContour(0),
        iSegment(0),
        type(UNKNOWN),
        consumed_vertical_up(false),
        consumed_perimeter_right(false)
        {}

    // Parent object owning this intersection point.
    const SegmentedIntersectionLine *line;
    // Container with the source expolygon and its shrank copies, to be intersected by the line.
    const ExPolygonWithOffset       *expoly_with_offset;

    // Index of a contour in ExPolygonWithOffset, with which this vertical line intersects.
    size_t                           iContour;
    // Index of a segment in iContour, with which this vertical line intersects.
    size_t                           iSegment;

    // Kind of intersection. With the original contour, or with the inner offestted contour?
    // A vertical segment will be at least intersected by OUTER_LOW, OUTER_HIGH,
    // but it could be intersected with OUTER_LOW, INNER_LOW, INNER_HIGH, OUTER_HIGH,
    // and there may be more than one pair of INNER_LOW, INNER_HIGH between OUTER_LOW, OUTER_HIGH.
    enum SegmentIntersectionType {
        OUTER_LOW   = 0,
        OUTER_HIGH  = 1,
        INNER_LOW   = 2,
        INNER_HIGH  = 3,
        UNKNOWN     = -1
    };
    SegmentIntersectionType    type;

    // For the INNER_LOW type, this point may be connected to another INNER_LOW point following a perimeter contour.
    // For the INNER_HIGH type, this point may be connected to another INNER_HIGH point following a perimeter contour.
    // If INNER_LOW is connected to INNER_HIGH or vice versa,
    // one has to make sure the vertical infill line does not overlap with the connecting perimeter line.
    bool is_inner() const { return type == INNER_LOW  || type == INNER_HIGH; }
    bool is_outer() const { return type == OUTER_LOW  || type == OUTER_HIGH; }
    bool is_low  () const { return type == INNER_LOW  || type == OUTER_LOW; }
    bool is_high () const { return type == INNER_HIGH || type == OUTER_HIGH; }

    // Calculate a position of this intersection point. The position does not need to be necessary exact.
    Point       pos() const;

    // Returns 0, if this and other segments intersect at the hatching line.
    // Returns -1, if this intersection is below the other intersection on the hatching line.
    // Returns +1 otherwise. 
    int         ordering_along_line(const SegmentIntersection &other) const;

    // Compare two y intersection points given by rational numbers.
    bool        operator< (const SegmentIntersection &other) const;
    //    { return this->ordering_along_line(other) == -1; }
    bool        operator==(const SegmentIntersection &other) const { return this->ordering_along_line(other) == 0; }

    //FIXME legacy code, suporting the old graph traversal algorithm. Please remove.
    // Was this segment along the y axis consumed?
    // Up means up along the vertical segment.
    bool consumed_vertical_up;
    // Was a segment of the inner perimeter contour consumed?
    // Right means right from the vertical segment.
    bool consumed_perimeter_right;
};

// A single hathing line intersecting the ExPolygonWithOffset.
class SegmentedIntersectionLine
{
public:
    // Index of this vertical intersection line.
    size_t                              idx;
    // Position of the line along the X axis of the oriented bounding box.
//    coord_t                             x;
    // Position of this vertical intersection line, rotated to the world coordinate system.
    Point                               pos;
    // Direction of this vertical intersection line, rotated to the world coordinate system. The direction is not normalized to maintain a sufficient accuracy!
    Vector                              dir;
    // List of intersection points with polygons, sorted increasingly by the y axis.
    // The SegmentIntersection keeps a pointer to this object to access the start and direction of this line.
    std::vector<SegmentIntersection>    intersections;
};

// Return an intersection point of the parent SegmentedIntersectionLine with the segment of a parent ExPolygonWithOffset.
// The intersected segment of the ExPolygonWithOffset is addressed with (iContour, iSegment).
// When calling this method, the SegmentedIntersectionLine must not be parallel with the segment.
Point SegmentIntersection::pos() const
{
    // Get the two rays to be intersected.
    const Polygon &poly = this->expoly_with_offset->contour(this->iContour);
    // 30 bits + 1 signum bit.
    const Point   &seg_start = poly.points[(this->iSegment == 0) ? poly.points.size() - 1 : this->iSegment - 1];
    const Point   &seg_end   = poly.points[this->iSegment];
    // Point, vector of the segment.
    const Pointf   p1  = convert_to<Pointf>(seg_start);
    const Pointf   v1  = convert_to<Pointf>(seg_end - seg_start);
    // Point, vector of this hatching line.
    const Pointf   p2  = convert_to<Pointf>(line->pos);
    const Pointf   v2  = convert_to<Pointf>(line->dir);
    // Intersect the two rays.
    double denom = v1.x * v2.y - v2.x * v1.y;
    Point out;
    if (denom == 0.) {
        // Lines are collinear. As the pos() method is not supposed to be called on collinear vectors,
        // the source vectors are not quite collinear. Return the center of the contour segment.
        out = seg_start + seg_end;
        out.x >>= 1;
        out.y >>= 1;
    } else {
        // Find the intersection point.
        double t = (v2.x * (p1.y - p2.y) - v2.y * (p1.x - p2.x)) / denom;
        if (t < 0.)
            out = seg_start;
        else if (t > 1.)
            out = seg_end;
        else {
            out.x = coord_t(floor(p1.x + t * v1.x + 0.5));
            out.y = coord_t(floor(p1.y + t * v1.y + 0.5));
        }
    }
    return out;
}

static inline int signum(int64_t v) { return (v > 0) - (v < 0); }

// Returns 0, if this and other segments intersect at the hatching line.
// Returns -1, if this intersection is below the other intersection on the hatching line.
// Returns +1 otherwise. 
int SegmentIntersection::ordering_along_line(const SegmentIntersection &other) const
{
    assert(this->line == other.line);
    assert(this->expoly_with_offset == other.expoly_with_offset);

    if (this->iContour == other.iContour && this->iSegment == other.iSegment)
        return true;

    // Segment of this
    const Polygon &poly_a      = this->expoly_with_offset->contour(this->iContour);
    // 30 bits + 1 signum bit.
    const Point   &seg_start_a = poly_a.points[(this->iSegment == 0) ? poly_a.points.size() - 1 : this->iSegment - 1];
    const Point   &seg_end_a   = poly_a.points[this->iSegment];

    // Segment of other
    const Polygon &poly_b      = this->expoly_with_offset->contour(other.iContour);
    // 30 bits + 1 signum bit.
    const Point   &seg_start_b = poly_b.points[(other.iSegment == 0) ? poly_b.points.size() - 1 : other.iSegment - 1];
    const Point   &seg_end_b   = poly_b.points[other.iSegment];

    if (this->iContour == other.iContour) {
        if ((this->iSegment + 1) % poly_a.points.size() == other.iSegment) {
            // other.iSegment succeeds this->iSegment
			assert(seg_end_a == seg_start_b);
			// Avoid calling the 128bit x 128bit multiplication below if this->line intersects the common point.
			if (cross(this->line->dir, seg_end_b - this->line->pos) == 0)
				return 0;
        } else if ((other.iSegment + 1) % poly_a.points.size() == this->iSegment) {
            // this->iSegment succeeds other.iSegment
			assert(seg_start_a == seg_end_b);
			// Avoid calling the 128bit x 128bit multiplication below if this->line intersects the common point.
			if (cross(this->line->dir, seg_start_a - this->line->pos) == 0)
				return 0;
        } else {
            // General case.
        }
    }

    // First test, whether both points of one segment are completely in one half-plane of the other line.
    const Point vec_b = seg_end_b - seg_start_b;
    int side_start = signum(cross(vec_b, seg_start_a - seg_start_b));
    int side_end   = signum(cross(vec_b, seg_end_a   - seg_start_b));
    int side       = side_start * side_end;
    if (side > 0)
        // This segment is completely inside one half-plane of the other line, therefore the ordering is trivial.
        return signum(cross(vec_b, this->line->dir)) * side_start;

    const Point vec_a = seg_end_a - seg_start_a;
    int side_start2 = signum(cross(vec_a, seg_start_b - seg_start_a));
    int side_end2   = signum(cross(vec_a, seg_end_b   - seg_start_a));
    int side2       = side_start2 * side_end2;
    //if (side == 0 && side2 == 0)
        // The segments share one of their end points.
    if (side2 > 0)
        // This segment is completely inside one half-plane of the other line, therefore the ordering is trivial.
        return signum(cross(this->line->dir, vec_a)) * side_start2;

    // The two segments intersect and they are not sucessive segments of the same contour.
    // Ordering of the points depends on the position of the segment intersection (left / right from this->line),
    // therefore a simple test over the input segment end points is not sufficient.

    // Find the parameters of intersection of the two segmetns with this->line.
	int64_t denom1 = cross(this->line->dir, vec_a);
	int64_t denom2 = cross(this->line->dir, vec_b);
	Point   vx_a   = seg_start_a - this->line->pos;
	Point   vx_b   = seg_start_b - this->line->pos;
	int64_t t1_times_denom1 = int64_t(vx_a.x) * int64_t(vec_a.y) - int64_t(vx_a.y) * int64_t(vec_a.x);
	int64_t t2_times_denom2 = int64_t(vx_b.x) * int64_t(vec_b.y) - int64_t(vx_b.y) * int64_t(vec_b.x);
	assert(denom1 != 0);
    assert(denom2 != 0);
    return Int128::compare_rationals_filtered(t1_times_denom1, denom1, t2_times_denom2, denom2);
}

// Compare two y intersection points given by rational numbers.
bool SegmentIntersection::operator<(const SegmentIntersection &other) const
{
#ifdef _DEBUG
    Point p1 = this->pos();
    Point p2 = other.pos();
    int64_t d = dot(this->line->dir, p2 - p1);
#endif /* _DEBUG */
    int   ordering = this->ordering_along_line(other);
#ifdef _DEBUG
    if (ordering == -1)
        assert(d >= - int64_t(SCALED_EPSILON));
    else if (ordering == 1)
        assert(d <= int64_t(SCALED_EPSILON));
#endif /* _DEBUG */
    return ordering == -1;
}

// When doing a rectilinear / grid / triangle / stars / cubic infill,
// the following class holds the hatching lines of each of the hatching directions.
class InfillHatchingSingleDirection
{
public:
    // Hatching angle, CCW from the X axis.
    double                                  angle;
    // Starting point of the 1st hatching line.
    Point                                   start_point;
    // Direction vector, its size is not normalized to maintain a sufficient accuracy!
    Vector                                  direction;
    // Spacing of the hatching lines, perpendicular to the direction vector.
    coord_t                                 line_spacing;
    // Infill segments oriented at angle.
    std::vector<SegmentedIntersectionLine>  segs;
};

// For the rectilinear, grid, triangles, stars and cubic pattern fill one InfillHatchingSingleDirection structure
// for each infill direction. The segments stored in InfillHatchingSingleDirection will then form a graph of candidate
// paths to be extruded.
static bool prepare_infill_hatching_segments(
    // Input geometry to be hatch, containing two concentric contours for each input contour.
    const ExPolygonWithOffset      &poly_with_offset,
    // fill density, dont_adjust
    const FillParams               &params,
    // angle, pattern_shift, spacing
    FillRectilinear3::FillDirParams &fill_dir_params,
    // Reference point of the pattern, to which the infill lines will be alligned, and the base angle.
    const std::pair<float, Point>  &rotate_vector,
    // Resulting straight segments of the infill graph.
    InfillHatchingSingleDirection  &out)
{
    out.angle = rotate_vector.first + fill_dir_params.angle;
    out.direction = Point(coord_t(scale_(1000)), coord_t(0));
    // Hatch along the Y axis of the rotated coordinate system.
    out.direction.rotate(out.angle + 0.5 * M_PI);
    out.segs.clear();

    assert(params.density > 0.0001f && params.density <= 1.f);
    coord_t line_spacing = coord_t(scale_(fill_dir_params.spacing) / params.density);

    // Bounding box around the source contour, aligned with out.angle.
    BoundingBox bounding_box = get_extents_rotated(poly_with_offset.polygons_src.contour, - out.angle);

    // Define the flow spacing according to requested density.
    if (params.full_infill() && ! params.dont_adjust) {
        // Full infill, adjust the line spacing to fit an integer number of lines.
        out.line_spacing = Fill::_adjust_solid_spacing(bounding_box.size().x, line_spacing);
        // Report back the adjusted line spacing.
        fill_dir_params.spacing = float(unscale(line_spacing));
    } else {
        // Extend bounding box so that our pattern will be aligned with the other layers.
        // Transform the reference point to the rotated coordinate system.
        Point refpt = rotate_vector.second.rotated(- out.angle);
        // _align_to_grid will not work correctly with positive pattern_shift.
        coord_t pattern_shift_scaled = coord_t(scale_(fill_dir_params.pattern_shift)) % line_spacing;
        refpt.x -= (pattern_shift_scaled >= 0) ? pattern_shift_scaled : (line_spacing + pattern_shift_scaled);
        bounding_box.merge(Fill::_align_to_grid(
            bounding_box.min, 
            Point(line_spacing, line_spacing), 
            refpt));
    }

    // Intersect a set of euqally spaced vertical lines wiht expolygon.
    // n_vlines = ceil(bbox_width / line_spacing)
    size_t  n_vlines = (bounding_box.max.x - bounding_box.min.x + line_spacing - 1) / line_spacing;
    coord_t x0 = bounding_box.min.x;
    if (params.full_infill())
        x0 += coord_t((line_spacing + SCALED_EPSILON) / 2);

    out.line_spacing = line_spacing;
    out.start_point = Point(x0, bounding_box.min.y);
    out.start_point.rotate(out.angle);

#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    BoundingBox bbox_svg = poly_with_offset.bounding_box_outer();
    ::Slic3r::SVG svg(debug_out_path("FillRectilinear2-%d.svg", iRun), bbox_svg); // , scale_(1.));
    poly_with_offset.export_to_svg(svg);
    {
        ::Slic3r::SVG svg(debug_out_path("FillRectilinear2-initial-%d.svg", iRun), bbox_svg); // , scale_(1.));
        poly_with_offset.export_to_svg(svg);
    }
    iRun ++;
#endif /* SLIC3R_DEBUG */

    // For each contour
    // Allocate storage for the segments.
    out.segs.assign(n_vlines, SegmentedIntersectionLine());
    double cos_a = cos(out.angle);
    double sin_a = sin(out.angle);
    for (size_t i = 0; i < n_vlines; ++ i) {
        auto &seg = out.segs[i];
        seg.idx = i;
        // seg.x   = x0 + coord_t(i) * line_spacing;
        coord_t x = x0 + coord_t(i) * line_spacing;
        seg.pos.x = coord_t(floor(cos_a * x                  - sin_a * bounding_box.min.y + 0.5));
        seg.pos.y = coord_t(floor(cos_a * bounding_box.min.y + sin_a * x                  + 0.5));
        seg.dir = out.direction;
    }

    for (size_t iContour = 0; iContour < poly_with_offset.n_contours; ++ iContour) {
        const Points &contour = poly_with_offset.contour(iContour).points;
        if (contour.size() < 2)
            continue;
        // For each segment
        for (size_t iSegment = 0; iSegment < contour.size(); ++ iSegment) {
            size_t iPrev = ((iSegment == 0) ? contour.size() : iSegment) - 1;
            const Point *pl = &contour[iPrev];
            const Point *pr = &contour[iSegment];
            // Orient the segment to the direction vector.
            const Point  v  = *pr - *pl;
            int   orientation = Int128::sign_determinant_2x2_filtered(v.x, v.y, out.direction.x, out.direction.y);
            if (orientation == 0)
                // Ignore strictly vertical segments.
                continue;
            if (orientation < 0)
                // Always orient the input segment consistently towards the hatching direction.
                std::swap(pl, pr);
            // Which of the equally spaced vertical lines is intersected by this segment?
            coord_t l = (coord_t)floor(cos_a * pl->x + sin_a * pl->y - SCALED_EPSILON);
            coord_t r = (coord_t)ceil (cos_a * pr->x + sin_a * pr->y + SCALED_EPSILON);
			assert(l < r - SCALED_EPSILON);
            // il, ir are the left / right indices of vertical lines intersecting a segment
            int il = std::max<int>(0, (l - x0 + line_spacing) / line_spacing);
			int ir = std::min<int>(int(out.segs.size()) - 1, (r - x0) / line_spacing);
            // The previous tests were done with floating point arithmetics over an epsilon-extended interval.
            // Now do the same tests with exact arithmetics over the exact interval.
            while (il <= ir && Int128::orient(out.segs[il].pos, out.segs[il].pos + out.direction, *pl) < 0)
                ++ il;
            while (il <= ir && Int128::orient(out.segs[ir].pos, out.segs[ir].pos + out.direction, *pr) > 0)
                -- ir;
            // Here it is ensured, that
            // 1) out.seg is not parallel to (pl, pr)
            // 2) all lines from il to ir intersect <pl, pr>.
            assert(il >= 0 && ir < int(out.segs.size()));
            for (int i = il; i <= ir; ++ i) {
                // assert(out.segs[i].x == i * line_spacing + x0);
                // assert(l <= out.segs[i].x);
                // assert(r >= out.segs[i].x);
                SegmentIntersection is;
                is.line     = &out.segs[i];
                is.expoly_with_offset = &poly_with_offset;
                is.iContour = iContour;
				is.iSegment = iSegment;
                // Test whether the calculated intersection point falls into the bounding box of the input segment.
                // +-1 to take rounding into account.
				assert(Int128::orient(out.segs[i].pos, out.segs[i].pos + out.direction, *pl) >= 0);
				assert(Int128::orient(out.segs[i].pos, out.segs[i].pos + out.direction, *pr) <= 0);
                assert(is.pos().x + 1 >= std::min(pl->x, pr->x));
                assert(is.pos().y + 1 >= std::min(pl->y, pr->y));
                assert(is.pos().x     <= std::max(pl->x, pr->x) + 1);
                assert(is.pos().y     <= std::max(pl->y, pr->y) + 1);
                out.segs[i].intersections.push_back(is);
            }
        }
    }

    // Sort the intersections along their segments, specify the intersection types.
    for (size_t i_seg = 0; i_seg < out.segs.size(); ++ i_seg) {
        SegmentedIntersectionLine &sil = out.segs[i_seg];
        // Sort the intersection points using exact rational arithmetic.
        std::sort(sil.intersections.begin(), sil.intersections.end());
#ifdef _DEBUG
        // Verify that the intersections are sorted along the haching direction.
        for (size_t i = 1; i < sil.intersections.size(); ++ i) {
            Point p1 = sil.intersections[i - 1].pos();
            Point p2 = sil.intersections[i].pos();
            int64_t d = dot(sil.dir, p2 - p1);
            assert(d >= - int64_t(SCALED_EPSILON));
        }
#endif /* _DEBUG */
        // Assign the intersection types, remove duplicate or overlapping intersection points.
        // When a loop vertex touches a vertical line, intersection point is generated for both segments.
        // If such two segments are oriented equally, then one of them is removed.
        // Otherwise the vertex is tangential to the vertical line and both segments are removed.
        // The same rule applies, if the loop is pinched into a single point and this point touches the vertical line:
        // The loop has a zero vertical size at the vertical line, therefore the intersection point is removed.
        size_t j = 0;
        for (size_t i = 0; i < sil.intersections.size(); ++ i) {
            // What is the orientation of the segment at the intersection point?
            size_t iContour = sil.intersections[i].iContour;
            const Points &contour = poly_with_offset.contour(iContour).points;
            size_t iSegment = sil.intersections[i].iSegment;
            size_t iPrev    = ((iSegment == 0) ? contour.size() : iSegment) - 1;
			int    dir      = Int128::cross(contour[iSegment] - contour[iPrev], sil.dir);
            bool low = dir > 0;
            sil.intersections[i].type = poly_with_offset.is_contour_outer(iContour) ? 
                (low ? SegmentIntersection::OUTER_LOW : SegmentIntersection::OUTER_HIGH) :
                (low ? SegmentIntersection::INNER_LOW : SegmentIntersection::INNER_HIGH);
            if (j > 0 && sil.intersections[i].iContour == sil.intersections[j-1].iContour) {
                // Two successive intersection points on a vertical line with the same contour. This may be a special case.
                if (sil.intersections[i] == sil.intersections[j-1]) {
                    // Two successive segments meet exactly at the vertical line.
        #ifdef SLIC3R_DEBUG
                    // Verify that the segments of sil.intersections[i] and sil.intersections[j-1] are adjoint.
                    size_t iSegment2 = sil.intersections[j-1].iSegment;
                    size_t iPrev2    = ((iSegment2 == 0) ? contour.size() : iSegment2) - 1;
                    assert(iSegment == iPrev2 || iSegment2 == iPrev);
        #endif /* SLIC3R_DEBUG */
                    if (sil.intersections[i].type == sil.intersections[j-1].type) {
                        // Two successive segments of the same direction (both to the right or both to the left)
                        // meet exactly at the vertical line.
                        // Remove the second intersection point.
                    } else {
                        // This is a loop returning to the same point.
                        // It may as well be a vertex of a loop touching this vertical line.
                        // Remove both the lines.
                        -- j;
                    }
                } else if (sil.intersections[i].type == sil.intersections[j-1].type) {
                    // Two non successive segments of the same direction (both to the right or both to the left)
                    // meet exactly at the vertical line. That means there is a Z shaped path, where the center segment
                    // of the Z shaped path is aligned with this vertical line.
                    // Remove one of the intersection points while maximizing the vertical segment length.
                    if (low) {
                        // Remove the second intersection point, keep the first intersection point.
                    } else {
                        // Remove the first intersection point, keep the second intersection point.
                        sil.intersections[j-1] = sil.intersections[i];
                    }
                } else {
                    // Vertical line intersects a contour segment at a general position (not at one of its end points).
                    // or the contour just touches this vertical line with a vertical segment or a sequence of vertical segments.
                    // Keep both intersection points.
                    if (j < i)
                        sil.intersections[j] = sil.intersections[i];
                    ++ j;
                }
            } else {
                // Vertical line intersects a contour segment at a general position (not at one of its end points).
                if (j < i)
                    sil.intersections[j] = sil.intersections[i];
                ++ j;
            }
        }
        // Shrink the list of intersections, if any of the intersection was removed during the classification.
        if (j < sil.intersections.size())
            sil.intersections.erase(sil.intersections.begin() + j, sil.intersections.end());
    }

    // Verify the segments. If something is wrong, give up.
#define ASSERT_OR_RETURN(CONDITION) do { assert(CONDITION); if (! (CONDITION)) return false; } while (0)
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4127)
#endif
    for (size_t i_seg = 0; i_seg < out.segs.size(); ++ i_seg) {
        SegmentedIntersectionLine &sil = out.segs[i_seg];
        // The intersection points have to be even.
        ASSERT_OR_RETURN((sil.intersections.size() & 1) == 0);
        for (size_t i = 0; i < sil.intersections.size();) {
            // An intersection segment crossing the bigger contour may cross the inner offsetted contour even number of times.
            ASSERT_OR_RETURN(sil.intersections[i].type == SegmentIntersection::OUTER_LOW);
            size_t j = i + 1;
            ASSERT_OR_RETURN(j < sil.intersections.size());
            ASSERT_OR_RETURN(sil.intersections[j].type == SegmentIntersection::INNER_LOW || sil.intersections[j].type == SegmentIntersection::OUTER_HIGH);
            for (; j < sil.intersections.size() && sil.intersections[j].is_inner(); ++ j) ;
            ASSERT_OR_RETURN(j < sil.intersections.size());
            ASSERT_OR_RETURN((j & 1) == 1);
            ASSERT_OR_RETURN(sil.intersections[j].type == SegmentIntersection::OUTER_HIGH);
            ASSERT_OR_RETURN(i + 1 == j || sil.intersections[j - 1].type == SegmentIntersection::INNER_HIGH);
            i = j + 1;
        }
    }
#undef ASSERT_OR_RETURN
#ifdef _MSC_VER
    #pragma warning(push)
#endif /* _MSC_VER */

#ifdef SLIC3R_DEBUG
    // Paint the segments and finalize the SVG file.
    for (size_t i_seg = 0; i_seg < out.segs.size(); ++ i_seg) {
        SegmentedIntersectionLine &sil = out.segs[i_seg];
        for (size_t i = 0; i < sil.intersections.size();) {
            size_t j = i + 1;
            for (; j < sil.intersections.size() && sil.intersections[j].is_inner(); ++ j) ;
            if (i + 1 == j) {
                svg.draw(Line(sil.intersections[i  ].pos(), sil.intersections[j  ].pos()), "blue");
            } else {
                svg.draw(Line(sil.intersections[i  ].pos(), sil.intersections[i+1].pos()), "green");
                svg.draw(Line(sil.intersections[i+1].pos(), sil.intersections[j-1].pos()), (j - i + 1 > 4) ? "yellow" : "magenta");
                svg.draw(Line(sil.intersections[j-1].pos(), sil.intersections[j  ].pos()), "green");
            }
            i = j + 1;
        }
    }
    svg.Close();
#endif /* SLIC3R_DEBUG */


    return true;
}








/****************************************************************** Legacy code, to be replaced by a graph algorithm ******************************************************************/


// Having a segment of a closed polygon, calculate its Euclidian length.
// The segment indices seg1 and seg2 signify an end point of an edge in the forward direction of the loop,
// therefore the point p1 lies on poly.points[seg1-1], poly.points[seg1] etc.
static inline coordf_t segment_length(const Polygon &poly, size_t seg1, const Point &p1, size_t seg2, const Point &p2)
{
#ifdef SLIC3R_DEBUG
    // Verify that p1 lies on seg1. This is difficult to verify precisely,
    // but at least verify, that p1 lies in the bounding box of seg1.
    for (size_t i = 0; i < 2; ++ i) {
        size_t seg = (i == 0) ? seg1 : seg2;
        Point  px  = (i == 0) ? p1   : p2;
        Point  pa  = poly.points[((seg == 0) ? poly.points.size() : seg) - 1];
        Point  pb  = poly.points[seg];
        if (pa.x > pb.x)
            std::swap(pa.x, pb.x);
        if (pa.y > pb.y)
            std::swap(pa.y, pb.y);
        assert(px.x >= pa.x && px.x <= pb.x);
        assert(px.y >= pa.y && px.y <= pb.y);
    }
#endif /* SLIC3R_DEBUG */
    const Point *pPrev = &p1;
    const Point *pThis = NULL;
    coordf_t len = 0;
    if (seg1 <= seg2) {
        for (size_t i = seg1; i < seg2; ++ i, pPrev = pThis)
           len += pPrev->distance_to(*(pThis = &poly.points[i]));
    } else {
        for (size_t i = seg1; i < poly.points.size(); ++ i, pPrev = pThis)
           len += pPrev->distance_to(*(pThis = &poly.points[i]));
        for (size_t i = 0; i < seg2; ++ i, pPrev = pThis)
           len += pPrev->distance_to(*(pThis = &poly.points[i]));
    }
    len += pPrev->distance_to(p2);
    return len;
}

// Append a segment of a closed polygon to a polyline.
// The segment indices seg1 and seg2 signify an end point of an edge in the forward direction of the loop.
// Only insert intermediate points between seg1 and seg2.
static inline void polygon_segment_append(Points &out, const Polygon &polygon, size_t seg1, size_t seg2)
{
    if (seg1 == seg2) {
        // Nothing to append from this segment.
    } else if (seg1 < seg2) {
        // Do not append a point pointed to by seg2.
        out.insert(out.end(), polygon.points.begin() + seg1, polygon.points.begin() + seg2);
    } else {
        out.reserve(out.size() + seg2 + polygon.points.size() - seg1);
        out.insert(out.end(), polygon.points.begin() + seg1, polygon.points.end());
        // Do not append a point pointed to by seg2.
        out.insert(out.end(), polygon.points.begin(), polygon.points.begin() + seg2);
    }
}

// Append a segment of a closed polygon to a polyline.
// The segment indices seg1 and seg2 signify an end point of an edge in the forward direction of the loop,
// but this time the segment is traversed backward.
// Only insert intermediate points between seg1 and seg2.
static inline void polygon_segment_append_reversed(Points &out, const Polygon &polygon, size_t seg1, size_t seg2)
{
    if (seg1 >= seg2) {
        out.reserve(seg1 - seg2);
        for (size_t i = seg1; i > seg2; -- i)
            out.push_back(polygon.points[i - 1]);
    } else {
        // it could be, that seg1 == seg2. In that case, append the complete loop.
        out.reserve(out.size() + seg2 + polygon.points.size() - seg1);
        for (size_t i = seg1; i > 0; -- i)
            out.push_back(polygon.points[i - 1]);
        for (size_t i = polygon.points.size(); i > seg2; -- i)
            out.push_back(polygon.points[i - 1]);
    }
}

static inline int distance_of_segmens(const Polygon &poly, size_t seg1, size_t seg2, bool forward)
{
    int d = int(seg2) - int(seg1);
    if (! forward)
        d = - d;
    if (d < 0)
        d += int(poly.points.size());
    return d;
}

// For a vertical line, an inner contour and an intersection point,
// find an intersection point on the previous resp. next vertical line.
// The intersection point is connected with the prev resp. next intersection point with iInnerContour.
// Return -1 if there is no such point on the previous resp. next vertical line.
static inline int intersection_on_prev_next_vertical_line(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iInnerContour,
    size_t                                         iIntersection,
    bool                                           dir_is_next)
{
    size_t iVerticalLineOther = iVerticalLine;
    if (dir_is_next) {
        if (++ iVerticalLineOther == segs.size())
            // No successive vertical line.
            return -1;
    } else if (iVerticalLineOther -- == 0) {
        // No preceding vertical line.
        return -1;
    }

    const SegmentedIntersectionLine &il    = segs[iVerticalLine];
    const SegmentIntersection       &itsct = il.intersections[iIntersection];
    const SegmentedIntersectionLine &il2   = segs[iVerticalLineOther];
    const Polygon                   &poly  = poly_with_offset.contour(iInnerContour);
//    const bool                       ccw   = poly_with_offset.is_contour_ccw(iInnerContour);
    const bool                       forward = itsct.is_low() == dir_is_next;
    // Resulting index of an intersection point on il2.
    int                              out   = -1;
    // Find an intersection point on iVerticalLineOther, intersecting iInnerContour
    // at the same orientation as iIntersection, and being closest to iIntersection
    // in the number of contour segments, when following the direction of the contour.
    int                              dmin  = std::numeric_limits<int>::max();
    for (size_t i = 0; i < il2.intersections.size(); ++ i) {
        const SegmentIntersection &itsct2 = il2.intersections[i];
        if (itsct.iContour == itsct2.iContour && itsct.type == itsct2.type) {
            /*
            if (itsct.is_low()) {
                assert(itsct.type == SegmentIntersection::INNER_LOW);
                assert(iIntersection > 0);
                assert(il.intersections[iIntersection-1].type == SegmentIntersection::OUTER_LOW);                
                assert(i > 0);
                if (il2.intersections[i-1].is_inner())
                    // Take only the lowest inner intersection point.
                    continue;
                assert(il2.intersections[i-1].type == SegmentIntersection::OUTER_LOW);
            } else {
                assert(itsct.type == SegmentIntersection::INNER_HIGH);
                assert(iIntersection+1 < il.intersections.size());
                assert(il.intersections[iIntersection+1].type == SegmentIntersection::OUTER_HIGH);
                assert(i+1 < il2.intersections.size());
                if (il2.intersections[i+1].is_inner())
                    // Take only the highest inner intersection point.
                    continue;
                assert(il2.intersections[i+1].type == SegmentIntersection::OUTER_HIGH);
            }
            */
            // The intersection points lie on the same contour and have the same orientation.
            // Find the intersection point with a shortest path in the direction of the contour.
            int d = distance_of_segmens(poly, itsct.iSegment, itsct2.iSegment, forward);
            if (d < dmin) {
                out = i;
                dmin = d;
            }
        }
    }
    //FIXME this routine is not asymptotic optimal, it will be slow if there are many intersection points along the line.
    return out;
}

static inline int intersection_on_prev_vertical_line(
    const ExPolygonWithOffset                     &poly_with_offset, 
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iInnerContour,
    size_t                                         iIntersection)
{
    return intersection_on_prev_next_vertical_line(poly_with_offset, segs, iVerticalLine, iInnerContour, iIntersection, false);
}

static inline int intersection_on_next_vertical_line(
    const ExPolygonWithOffset                     &poly_with_offset, 
    const std::vector<SegmentedIntersectionLine>  &segs, 
    size_t                                         iVerticalLine, 
    size_t                                         iInnerContour, 
    size_t                                         iIntersection)
{
    return intersection_on_prev_next_vertical_line(poly_with_offset, segs, iVerticalLine, iInnerContour, iIntersection, true);
}

enum IntersectionTypeOtherVLine {
    // There is no connection point on the other vertical line.
    INTERSECTION_TYPE_OTHER_VLINE_UNDEFINED = -1,
    // Connection point on the other vertical segment was found
    // and it could be followed.
    INTERSECTION_TYPE_OTHER_VLINE_OK = 0,
    // The connection segment connects to a middle of a vertical segment.
    // Cannot follow.
    INTERSECTION_TYPE_OTHER_VLINE_INNER,
    // Cannot extend the contor to this intersection point as either the connection segment
    // or the succeeding vertical segment were already consumed.
    INTERSECTION_TYPE_OTHER_VLINE_CONSUMED,
    // Not the first intersection along the contor. This intersection point
    // has been preceded by an intersection point along the vertical line.
    INTERSECTION_TYPE_OTHER_VLINE_NOT_FIRST,
};

// Find an intersection on a previous line, but return -1, if the connecting segment of a perimeter was already extruded.
static inline IntersectionTypeOtherVLine intersection_type_on_prev_next_vertical_line(
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iIntersection,
    size_t                                         iIntersectionOther,
    bool                                           dir_is_next)
{
    // This routine will propose a connecting line even if the connecting perimeter segment intersects 
    // iVertical line multiple times before reaching iIntersectionOther.
    if (iIntersectionOther == -1)
        return INTERSECTION_TYPE_OTHER_VLINE_UNDEFINED;
    assert(dir_is_next ? (iVerticalLine + 1 < segs.size()) : (iVerticalLine > 0));
    const SegmentedIntersectionLine &il_this      = segs[iVerticalLine];
    const SegmentIntersection       &itsct_this   = il_this.intersections[iIntersection];
    const SegmentedIntersectionLine &il_other     = segs[dir_is_next ? (iVerticalLine+1) : (iVerticalLine-1)];
    const SegmentIntersection       &itsct_other  = il_other.intersections[iIntersectionOther];
    assert(itsct_other.is_inner());
    assert(iIntersectionOther > 0);
    assert(iIntersectionOther + 1 < il_other.intersections.size());
    // Is iIntersectionOther at the boundary of a vertical segment?
    const SegmentIntersection       &itsct_other2 = il_other.intersections[itsct_other.is_low() ? iIntersectionOther - 1 : iIntersectionOther + 1];
    if (itsct_other2.is_inner())
        // Cannot follow a perimeter segment into the middle of another vertical segment.
        // Only perimeter segments connecting to the end of a vertical segment are followed.
        return INTERSECTION_TYPE_OTHER_VLINE_INNER;
    assert(itsct_other.is_low() == itsct_other2.is_low());
    if (dir_is_next ? itsct_this.consumed_perimeter_right : itsct_other.consumed_perimeter_right)
        // This perimeter segment was already consumed.
        return INTERSECTION_TYPE_OTHER_VLINE_CONSUMED;
    if (itsct_other.is_low() ? itsct_other.consumed_vertical_up : il_other.intersections[iIntersectionOther-1].consumed_vertical_up)
        // This vertical segment was already consumed.
        return INTERSECTION_TYPE_OTHER_VLINE_CONSUMED;
    return INTERSECTION_TYPE_OTHER_VLINE_OK;
}

static inline IntersectionTypeOtherVLine intersection_type_on_prev_vertical_line(
    const std::vector<SegmentedIntersectionLine>  &segs, 
    size_t                                         iVerticalLine, 
    size_t                                         iIntersection,
    size_t                                         iIntersectionPrev)
{
    return intersection_type_on_prev_next_vertical_line(segs, iVerticalLine, iIntersection, iIntersectionPrev, false);
}

static inline IntersectionTypeOtherVLine intersection_type_on_next_vertical_line(
    const std::vector<SegmentedIntersectionLine>  &segs, 
    size_t                                         iVerticalLine, 
    size_t                                         iIntersection,
    size_t                                         iIntersectionNext)
{
    return intersection_type_on_prev_next_vertical_line(segs, iVerticalLine, iIntersection, iIntersectionNext, true);
}

// Measure an Euclidian length of a perimeter segment when going from iIntersection to iIntersection2.
static inline coordf_t measure_perimeter_prev_next_segment_length(
    const ExPolygonWithOffset                     &poly_with_offset, 
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iInnerContour,
    size_t                                         iIntersection,
    size_t                                         iIntersection2,
    bool                                           dir_is_next)
{
    size_t iVerticalLineOther = iVerticalLine;
    if (dir_is_next) {
        if (++ iVerticalLineOther == segs.size())
            // No successive vertical line.
            return coordf_t(-1);
    } else if (iVerticalLineOther -- == 0) {
        // No preceding vertical line.
        return coordf_t(-1);
    }

    const SegmentedIntersectionLine &il     = segs[iVerticalLine];
    const SegmentIntersection       &itsct  = il.intersections[iIntersection];
    const SegmentedIntersectionLine &il2    = segs[iVerticalLineOther];
    const SegmentIntersection       &itsct2 = il2.intersections[iIntersection2];
    const Polygon                   &poly   = poly_with_offset.contour(iInnerContour);
//    const bool                       ccw    = poly_with_offset.is_contour_ccw(iInnerContour);
    assert(itsct.type == itsct2.type);
    assert(itsct.iContour == itsct2.iContour);
    assert(itsct.is_inner());
    const bool                       forward = itsct.is_low() == dir_is_next;

    Point p1 = itsct.pos();
    Point p2 = itsct2.pos();
    return forward ?
        segment_length(poly, itsct .iSegment, p1, itsct2.iSegment, p2) :
        segment_length(poly, itsct2.iSegment, p2, itsct .iSegment, p1);
}

static inline coordf_t measure_perimeter_prev_segment_length(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iInnerContour,
    size_t                                         iIntersection,
    size_t                                         iIntersection2)
{
    return measure_perimeter_prev_next_segment_length(poly_with_offset, segs, iVerticalLine, iInnerContour, iIntersection, iIntersection2, false);
}

static inline coordf_t measure_perimeter_next_segment_length(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iInnerContour,
    size_t                                         iIntersection,
    size_t                                         iIntersection2)
{
    return measure_perimeter_prev_next_segment_length(poly_with_offset, segs, iVerticalLine, iInnerContour, iIntersection, iIntersection2, true);
}

// Append the points of a perimeter segment when going from iIntersection to iIntersection2.
// The first point (the point of iIntersection) will not be inserted,
// the last point will be inserted.
static inline void emit_perimeter_prev_next_segment(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iInnerContour,
    size_t                                         iIntersection,
    size_t                                         iIntersection2,
    Polyline                                      &out,
    bool                                           dir_is_next)
{
    size_t iVerticalLineOther = iVerticalLine;
    if (dir_is_next) {
        ++ iVerticalLineOther;
        assert(iVerticalLineOther < segs.size());
    } else {
        assert(iVerticalLineOther > 0);
        -- iVerticalLineOther;
    }

    const SegmentedIntersectionLine &il     = segs[iVerticalLine];
    const SegmentIntersection       &itsct  = il.intersections[iIntersection];
    const SegmentedIntersectionLine &il2    = segs[iVerticalLineOther];
    const SegmentIntersection       &itsct2 = il2.intersections[iIntersection2];
    const Polygon                   &poly   = poly_with_offset.contour(iInnerContour);
//    const bool                       ccw    = poly_with_offset.is_contour_ccw(iInnerContour);
    assert(itsct.type == itsct2.type);
    assert(itsct.iContour == itsct2.iContour);
    assert(itsct.is_inner());
    const bool                       forward = itsct.is_low() == dir_is_next;
    // Do not append the first point.
    // out.points.push_back(Point(il.pos, itsct.pos));
    if (forward)
        polygon_segment_append(out.points, poly, itsct.iSegment, itsct2.iSegment);
    else
        polygon_segment_append_reversed(out.points, poly, itsct.iSegment, itsct2.iSegment);
    // Append the last point.
    out.points.push_back(itsct2.pos());
}

static inline coordf_t measure_perimeter_segment_on_vertical_line_length(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iInnerContour,
    size_t                                         iIntersection,
    size_t                                         iIntersection2,
    bool                                           forward)
{
    const SegmentedIntersectionLine &il = segs[iVerticalLine];
    const SegmentIntersection       &itsct = il.intersections[iIntersection];
    const SegmentIntersection       &itsct2 = il.intersections[iIntersection2];
    const Polygon                   &poly = poly_with_offset.contour(iInnerContour);
    assert(itsct.is_inner());
    assert(itsct2.is_inner());
    assert(itsct.type != itsct2.type);
    assert(itsct.iContour == iInnerContour);
    assert(itsct.iContour == itsct2.iContour);
    return forward ?
        segment_length(poly, itsct .iSegment, itsct.pos(),  itsct2.iSegment, itsct2.pos()) :
        segment_length(poly, itsct2.iSegment, itsct2.pos(), itsct .iSegment, itsct.pos());
}

// Append the points of a perimeter segment when going from iIntersection to iIntersection2.
// The first point (the point of iIntersection) will not be inserted,
// the last point will be inserted.
static inline void emit_perimeter_segment_on_vertical_line(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iInnerContour,
    size_t                                         iIntersection,
    size_t                                         iIntersection2,
    Polyline                                      &out,
    bool                                           forward)
{
    const SegmentedIntersectionLine &il = segs[iVerticalLine];
    const SegmentIntersection       &itsct = il.intersections[iIntersection];
    const SegmentIntersection       &itsct2 = il.intersections[iIntersection2];
    const Polygon                   &poly = poly_with_offset.contour(iInnerContour);
    assert(itsct.is_inner());
    assert(itsct2.is_inner());
    assert(itsct.type != itsct2.type);
    assert(itsct.iContour == iInnerContour);
    assert(itsct.iContour == itsct2.iContour);
    // Do not append the first point.
    // out.points.push_back(Point(il.pos, itsct.pos));
    if (forward)
        polygon_segment_append(out.points, poly, itsct.iSegment, itsct2.iSegment);
    else
        polygon_segment_append_reversed(out.points, poly, itsct.iSegment, itsct2.iSegment);
    // Append the last point.
    out.points.push_back(itsct2.pos());
}

//TBD: For precise infill, measure the area of a slab spanned by an infill line.
/*
static inline float measure_outer_contour_slab(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         i_vline,
    size_t                                         iIntersection)
{
    const SegmentedIntersectionLine &il     = segs[i_vline];
    const SegmentIntersection       &itsct  = il.intersections[i_vline];
    const SegmentIntersection       &itsct2 = il.intersections[iIntersection2];
    const Polygon                   &poly   = poly_with_offset.contour((itsct.iContour);
    assert(itsct.is_outer());
    assert(itsct2.is_outer());
    assert(itsct.type != itsct2.type);
    assert(itsct.iContour == itsct2.iContour);
    if (! itsct.is_outer() || ! itsct2.is_outer() || itsct.type == itsct2.type || itsct.iContour != itsct2.iContour)
        // Error, return zero area.
        return 0.f;

    // Find possible connection points on the previous / next vertical line.
    int iPrev = intersection_on_prev_vertical_line(poly_with_offset, segs, i_vline, itsct.iContour, i_intersection);
    int iNext = intersection_on_next_vertical_line(poly_with_offset, segs, i_vline, itsct.iContour, i_intersection);
    // Find possible connection points on the same vertical line.
    int iAbove = iBelow = -1;
    // Does the perimeter intersect the current vertical line above intrsctn?
    for (size_t i = i_intersection + 1; i + 1 < seg.intersections.size(); ++ i)
        if (seg.intersections[i].iContour == itsct.iContour)
            { iAbove = i; break; }
    // Does the perimeter intersect the current vertical line below intrsctn?
    for (int i = int(i_intersection) - 1; i > 0; -- i)
        if (seg.intersections[i].iContour == itsct.iContour)
            { iBelow = i; break; }

    if (iSegAbove != -1 && seg.intersections[iAbove].type == SegmentIntersection::OUTER_HIGH) {
        // Invalidate iPrev resp. iNext, if the perimeter crosses the current vertical line earlier than iPrev resp. iNext.
        // The perimeter contour orientation.
        const Polygon &poly = poly_with_offset.contour(itsct.iContour);
        {
            int d_horiz = (iPrev  == -1) ? std::numeric_limits<int>::max() :
                distance_of_segmens(poly, segs[i_vline-1].intersections[iPrev].iSegment, itsct.iSegment, true);
            int d_down  = (iBelow == -1) ? std::numeric_limits<int>::max() :
                distance_of_segmens(poly, iSegBelow, itsct.iSegment, true);
            int d_up    = (iAbove == -1) ? std::numeric_limits<int>::max() :
                distance_of_segmens(poly, iSegAbove, itsct.iSegment, true);
            if (intrsctn_type_prev == INTERSECTION_TYPE_OTHER_VLINE_OK && d_horiz > std::min(d_down, d_up))
                // The vertical crossing comes eralier than the prev crossing.
                // Disable the perimeter going back.
                intrsctn_type_prev = INTERSECTION_TYPE_OTHER_VLINE_NOT_FIRST;
            if (d_up > std::min(d_horiz, d_down))
                // The horizontal crossing comes earlier than the vertical crossing.
                vert_seg_dir_valid_mask &= ~DIR_BACKWARD;
        }
        {
            int d_horiz = (iNext     == -1) ? std::numeric_limits<int>::max() :
                distance_of_segmens(poly, itsct.iSegment, segs[i_vline+1].intersections[iNext].iSegment, true);
            int d_down  = (iSegBelow == -1) ? std::numeric_limits<int>::max() :
                distance_of_segmens(poly, itsct.iSegment, iSegBelow, true);
            int d_up    = (iSegAbove == -1) ? std::numeric_limits<int>::max() :
                distance_of_segmens(poly, itsct.iSegment, iSegAbove, true);
            if (d_up > std::min(d_horiz, d_down))
                // The horizontal crossing comes earlier than the vertical crossing.
                vert_seg_dir_valid_mask &= ~DIR_FORWARD;
        }
    }
}
*/

enum DirectionMask
{
    DIR_FORWARD  = 1,
    DIR_BACKWARD = 2
};

// For the rectilinear, grid, triangles, stars and cubic pattern fill one InfillHatchingSingleDirection structure
// for each infill direction. The segments stored in InfillHatchingSingleDirection will then form a graph of candidate
// paths to be extruded.
static bool fill_hatching_segments_legacy(
    // Input geometry to be hatch, containing two concentric contours for each input contour.
    const ExPolygonWithOffset      &poly_with_offset,
    // fill density, dont_adjust
    const FillParams               &params,
    const coord_t                   link_max_length,
    // Resulting straight segments of the infill graph.
    InfillHatchingSingleDirection  &hatching,
    Polylines                      &polylines_out)
{
    // At the end, only the new polylines will be rotated back.
    size_t n_polylines_out_initial = polylines_out.size();

    std::vector<SegmentedIntersectionLine> &segs = hatching.segs;

    // For each outer only chords, measure their maximum distance to the bow of the outer contour.
    // Mark an outer only chord as consumed, if the distance is low.
    for (size_t i_vline = 0; i_vline < segs.size(); ++ i_vline) {
        SegmentedIntersectionLine &seg = segs[i_vline];
        for (size_t i_intersection = 0; i_intersection + 1 < seg.intersections.size(); ++ i_intersection) {
            if (seg.intersections[i_intersection].type == SegmentIntersection::OUTER_LOW &&
                seg.intersections[i_intersection+1].type == SegmentIntersection::OUTER_HIGH) {
                bool consumed = false;
//                if (params.full_infill()) {
//                        measure_outer_contour_slab(poly_with_offset, segs, i_vline, i_ntersection);
//                } else
                    consumed = true;
                seg.intersections[i_intersection].consumed_vertical_up = consumed;
            }
        }
    }

    // Now construct a graph.
    // Find the first point.
    // Naively one would expect to achieve best results by chaining the paths by the shortest distance,
    // but that procedure does not create the longest continuous paths.
    // A simple "sweep left to right" procedure achieves better results.
    size_t    i_vline = 0;
    size_t    i_intersection = size_t(-1);
    // Follow the line, connect the lines into a graph.
    // Until no new line could be added to the output path:
    Point     pointLast;
    Polyline *polyline_current = NULL;
    if (! polylines_out.empty())
        pointLast = polylines_out.back().points.back();
    for (;;) {
        if (i_intersection == size_t(-1)) {
            // The path has been interrupted. Find a next starting point, closest to the previous extruder position.
            coordf_t dist2min = std::numeric_limits<coordf_t>().max();
            for (size_t i_vline2 = 0; i_vline2 < segs.size(); ++ i_vline2) {
                const SegmentedIntersectionLine &seg = segs[i_vline2];
                if (! seg.intersections.empty()) {
                    assert(seg.intersections.size() > 1);
                    // Even number of intersections with the loops.
                    assert((seg.intersections.size() & 1) == 0);
                    assert(seg.intersections.front().type == SegmentIntersection::OUTER_LOW);
                    for (size_t i = 0; i < seg.intersections.size(); ++ i) {
                        const SegmentIntersection &intrsctn = seg.intersections[i];
                        if (intrsctn.is_outer()) {
                            assert(intrsctn.is_low() || i > 0);
                            bool consumed = intrsctn.is_low() ? 
                                intrsctn.consumed_vertical_up : 
                                seg.intersections[i-1].consumed_vertical_up;
                            if (! consumed) {
                                coordf_t dist2 = pointLast.distance_to(intrsctn.pos());
                                if (dist2 < dist2min) {
                                    dist2min = dist2;
                                    i_vline = i_vline2;
                                    i_intersection = i;
                                    //FIXME We are taking the first left point always. Verify, that the caller chains the paths
                                    // by a shortest distance, while reversing the paths if needed.
                                    //if (polylines_out.empty())
                                        // Initial state, take the first line, which is the first from the left.
                                        goto found;
                                }
                            }
                        }
                    }
                }
            }
            if (i_intersection == size_t(-1))
                // We are finished.
                break;
        found:
            // Start a new path.
            polylines_out.push_back(Polyline());
            polyline_current = &polylines_out.back();
            // Emit the first point of a path.
            pointLast = segs[i_vline].intersections[i_intersection].pos();
            polyline_current->points.push_back(pointLast);
        }

        // From the initial point (i_vline, i_intersection), follow a path.
        SegmentedIntersectionLine &seg      = segs[i_vline];
        SegmentIntersection       *intrsctn = &seg.intersections[i_intersection];
        bool going_up = intrsctn->is_low();
        bool try_connect = false;
        if (going_up) {
            assert(! intrsctn->consumed_vertical_up);
            assert(i_intersection + 1 < seg.intersections.size());
            // Step back to the beginning of the vertical segment to mark it as consumed.
            if (intrsctn->is_inner()) {
                assert(i_intersection > 0);
                -- intrsctn;
                -- i_intersection;
            }
            // Consume the complete vertical segment up to the outer contour.
            do {
                intrsctn->consumed_vertical_up = true;
                ++ intrsctn;
                ++ i_intersection;
                assert(i_intersection < seg.intersections.size());
            } while (intrsctn->type != SegmentIntersection::OUTER_HIGH);
            if ((intrsctn - 1)->is_inner()) {
                // Step back.
                -- intrsctn;
                -- i_intersection;
                assert(intrsctn->type == SegmentIntersection::INNER_HIGH);
                try_connect = true;
            }
        } else {
            // Going down.
            assert(intrsctn->is_high());
            assert(i_intersection > 0);
            assert(! (intrsctn - 1)->consumed_vertical_up);
            // Consume the complete vertical segment up to the outer contour.
            if (intrsctn->is_inner())
                intrsctn->consumed_vertical_up = true;
            do {
                assert(i_intersection > 0);
                -- intrsctn;
                -- i_intersection;
                intrsctn->consumed_vertical_up = true;
            } while (intrsctn->type != SegmentIntersection::OUTER_LOW);
            if ((intrsctn + 1)->is_inner()) {
                // Step back.
                ++ intrsctn;
                ++ i_intersection;
                assert(intrsctn->type == SegmentIntersection::INNER_LOW);
                try_connect = true;
            }
        }
        if (try_connect) {
            // Decide, whether to finish the segment, or whether to follow the perimeter.

            // 1) Find possible connection points on the previous / next vertical line.
            int iPrev = intersection_on_prev_vertical_line(poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection);
            int iNext = intersection_on_next_vertical_line(poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection);
            IntersectionTypeOtherVLine intrsctn_type_prev = intersection_type_on_prev_vertical_line(segs, i_vline, i_intersection, iPrev);
            IntersectionTypeOtherVLine intrsctn_type_next = intersection_type_on_next_vertical_line(segs, i_vline, i_intersection, iNext);

            // 2) Find possible connection points on the same vertical line.
            int iAbove = -1;
            int iBelow = -1;
            int iSegAbove = -1;
            int iSegBelow = -1;
            {
                SegmentIntersection::SegmentIntersectionType type_crossing = (intrsctn->type == SegmentIntersection::INNER_LOW) ?
                    SegmentIntersection::INNER_HIGH : SegmentIntersection::INNER_LOW;
                // Does the perimeter intersect the current vertical line above intrsctn?
                for (size_t i = i_intersection + 1; i + 1 < seg.intersections.size(); ++ i)
//                    if (seg.intersections[i].iContour == intrsctn->iContour && seg.intersections[i].type == type_crossing) {
                    if (seg.intersections[i].iContour == intrsctn->iContour) {
                        iAbove = i;
                        iSegAbove = seg.intersections[i].iSegment;
                        break;
                    }
                // Does the perimeter intersect the current vertical line below intrsctn?
                for (size_t i = i_intersection - 1; i > 0; -- i)
//                    if (seg.intersections[i].iContour == intrsctn->iContour && seg.intersections[i].type == type_crossing) {
                    if (seg.intersections[i].iContour == intrsctn->iContour) {
                        iBelow = i;
                        iSegBelow = seg.intersections[i].iSegment;
                        break;
                    }
            }

            // 3) Sort the intersection points, clear iPrev / iNext / iSegBelow / iSegAbove,
            // if it is preceded by any other intersection point along the contour.
            unsigned int vert_seg_dir_valid_mask = 
                (going_up ? 
                    (iSegAbove != -1 && seg.intersections[iAbove].type == SegmentIntersection::INNER_LOW) :
                    (iSegBelow != -1 && seg.intersections[iBelow].type == SegmentIntersection::INNER_HIGH)) ?
                (DIR_FORWARD | DIR_BACKWARD) :
                0;
            {
                // Invalidate iPrev resp. iNext, if the perimeter crosses the current vertical line earlier than iPrev resp. iNext.
                // The perimeter contour orientation.
                const bool forward = intrsctn->is_low(); // == poly_with_offset.is_contour_ccw(intrsctn->iContour);
                const Polygon &poly = poly_with_offset.contour(intrsctn->iContour);
                {
                    int d_horiz = (iPrev     == -1) ? std::numeric_limits<int>::max() :
                        distance_of_segmens(poly, segs[i_vline-1].intersections[iPrev].iSegment, intrsctn->iSegment, forward);
                    int d_down  = (iSegBelow == -1) ? std::numeric_limits<int>::max() :
                        distance_of_segmens(poly, iSegBelow, intrsctn->iSegment, forward);
                    int d_up    = (iSegAbove == -1) ? std::numeric_limits<int>::max() :
                        distance_of_segmens(poly, iSegAbove, intrsctn->iSegment, forward);
                    if (intrsctn_type_prev == INTERSECTION_TYPE_OTHER_VLINE_OK && d_horiz > std::min(d_down, d_up))
                        // The vertical crossing comes eralier than the prev crossing.
                        // Disable the perimeter going back.
                        intrsctn_type_prev = INTERSECTION_TYPE_OTHER_VLINE_NOT_FIRST;
                    if (going_up ? (d_up > std::min(d_horiz, d_down)) : (d_down > std::min(d_horiz, d_up)))
                        // The horizontal crossing comes earlier than the vertical crossing.
                        vert_seg_dir_valid_mask &= ~(forward ? DIR_BACKWARD : DIR_FORWARD);
                }
                {
                    int d_horiz = (iNext     == -1) ? std::numeric_limits<int>::max() :
                        distance_of_segmens(poly, intrsctn->iSegment, segs[i_vline+1].intersections[iNext].iSegment, forward);
                    int d_down  = (iSegBelow == -1) ? std::numeric_limits<int>::max() :
                        distance_of_segmens(poly, intrsctn->iSegment, iSegBelow, forward);
                    int d_up    = (iSegAbove == -1) ? std::numeric_limits<int>::max() :
                        distance_of_segmens(poly, intrsctn->iSegment, iSegAbove, forward);
                    if (intrsctn_type_next == INTERSECTION_TYPE_OTHER_VLINE_OK && d_horiz > std::min(d_down, d_up))
                        // The vertical crossing comes eralier than the prev crossing.
                        // Disable the perimeter going forward.
                        intrsctn_type_next = INTERSECTION_TYPE_OTHER_VLINE_NOT_FIRST;
                    if (going_up ? (d_up > std::min(d_horiz, d_down)) : (d_down > std::min(d_horiz, d_up)))
                        // The horizontal crossing comes earlier than the vertical crossing.
                        vert_seg_dir_valid_mask &= ~(forward ? DIR_FORWARD : DIR_BACKWARD);
                }
            }

            // 4) Try to connect to a previous or next vertical line, making a zig-zag pattern.
            if (intrsctn_type_prev == INTERSECTION_TYPE_OTHER_VLINE_OK || intrsctn_type_next == INTERSECTION_TYPE_OTHER_VLINE_OK) {
                coordf_t distPrev = (intrsctn_type_prev != INTERSECTION_TYPE_OTHER_VLINE_OK) ? std::numeric_limits<coord_t>::max() :
                    measure_perimeter_prev_segment_length(poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection, iPrev);
                coordf_t distNext = (intrsctn_type_next != INTERSECTION_TYPE_OTHER_VLINE_OK) ? std::numeric_limits<coord_t>::max() :
                    measure_perimeter_next_segment_length(poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection, iNext);
                // Take the shorter path.
                //FIXME this may not be always the best strategy to take the shortest connection line now.
                bool take_next = (intrsctn_type_prev == INTERSECTION_TYPE_OTHER_VLINE_OK && intrsctn_type_next == INTERSECTION_TYPE_OTHER_VLINE_OK) ?
                    (distNext < distPrev) : 
                    intrsctn_type_next == INTERSECTION_TYPE_OTHER_VLINE_OK;
                assert(intrsctn->is_inner());
                bool skip = params.dont_connect || (link_max_length > 0 && (take_next ? distNext : distPrev) > link_max_length);
                if (skip) {
                    // Just skip the connecting contour and start a new path.
                    goto dont_connect;
                    polyline_current->points.push_back(intrsctn->pos());
                    polylines_out.push_back(Polyline()); 
                    polyline_current = &polylines_out.back(); 
                    const SegmentedIntersectionLine &il2 = segs[take_next ? (i_vline + 1) : (i_vline - 1)];
                    polyline_current->points.push_back(il2.intersections[take_next ? iNext : iPrev].pos());
                } else {
                    polyline_current->points.push_back(intrsctn->pos());
                    emit_perimeter_prev_next_segment(poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection, take_next ? iNext : iPrev, *polyline_current, take_next);
                }
                // Mark both the left and right connecting segment as consumed, because one cannot go to this intersection point as it has been consumed.
                if (iPrev != -1)
                    segs[i_vline-1].intersections[iPrev].consumed_perimeter_right = true;
                if (iNext != -1)
                    intrsctn->consumed_perimeter_right = true;
                //FIXME consume the left / right connecting segments at the other end of this line? Currently it is not critical because a perimeter segment is not followed if the vertical segment at the other side has already been consumed.
                // Advance to the neighbor line.
                if (take_next) {
                    ++ i_vline;
                    i_intersection = iNext;
                } else {
                    -- i_vline;
                    i_intersection = iPrev;
                }
                continue;
            } 

            // 5) Try to connect to a previous or next point on the same vertical line.
            if (vert_seg_dir_valid_mask) {
                bool valid = true;
                // Verify, that there is no intersection with the inner contour up to the end of the contour segment.
                // Verify, that the successive segment has not been consumed yet.
                if (going_up) {
                    if (seg.intersections[iAbove].consumed_vertical_up) {
                        valid = false;
                    } else {
                        for (int i = (int)i_intersection + 1; i < iAbove && valid; ++i)
                            if (seg.intersections[i].is_inner()) 
                                valid = false;
                    }
                } else {
                    if (seg.intersections[iBelow-1].consumed_vertical_up) {
                        valid = false;
                    } else {
                        for (int i = iBelow + 1; i < (int)i_intersection && valid; ++i)
                            if (seg.intersections[i].is_inner()) 
                                valid = false;
                    }
                }
                if (valid) {
                    const Polygon &poly = poly_with_offset.contour(intrsctn->iContour);
                    int iNext    = going_up ? iAbove : iBelow;
                    int iSegNext = going_up ? iSegAbove : iSegBelow;
                    bool dir_forward = (vert_seg_dir_valid_mask == (DIR_FORWARD | DIR_BACKWARD)) ?
                        // Take the shorter length between the current and the next intersection point.
                        (distance_of_segmens(poly, intrsctn->iSegment, iSegNext, true) <
                         distance_of_segmens(poly, intrsctn->iSegment, iSegNext, false)) :
                        (vert_seg_dir_valid_mask == DIR_FORWARD);
                    // Skip this perimeter line?
                    bool skip = params.dont_connect;
                    if (! skip && link_max_length > 0) {
                        coordf_t link_length = measure_perimeter_segment_on_vertical_line_length(
                            poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection, iNext, dir_forward);
                        skip = link_length > link_max_length;
                    }
                    polyline_current->points.push_back(intrsctn->pos());
                    if (skip) {
                        // Just skip the connecting contour and start a new path.
                        polylines_out.push_back(Polyline()); 
                        polyline_current = &polylines_out.back();
                        polyline_current->points.push_back(seg.intersections[iNext].pos());
                    } else {
                        // Consume the connecting contour and the next segment.
                        emit_perimeter_segment_on_vertical_line(poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection, iNext, *polyline_current, dir_forward);
                    }
                    // Mark both the left and right connecting segment as consumed, because one cannot go to this intersection point as it has been consumed.
                    // If there are any outer intersection points skipped (bypassed) by the contour,
                    // mark them as processed.
                    if (going_up) {
                        for (int i = (int)i_intersection; i < iAbove; ++ i)
                            seg.intersections[i].consumed_vertical_up = true;
                    } else {
                        for (int i = iBelow; i < (int)i_intersection; ++ i)
                            seg.intersections[i].consumed_vertical_up = true;
                    }
//                    seg.intersections[going_up ? i_intersection : i_intersection - 1].consumed_vertical_up = true;
                    intrsctn->consumed_perimeter_right = true;
                    i_intersection = iNext;
                    if (going_up)
                        ++ intrsctn;
                    else
                        -- intrsctn;
                    intrsctn->consumed_perimeter_right = true;
                    continue;
                }
            }
        dont_connect: 
            // No way to continue the current polyline. Take the rest of the line up to the outer contour.
            // This will finish the polyline, starting another polyline at a new point.
            if (going_up)
                ++ intrsctn;
            else
                -- intrsctn;
        }

        // Finish the current vertical line,
        // reset the current vertical line to pick a new starting point in the next round.
        assert(intrsctn->is_outer());
        assert(intrsctn->is_high() == going_up);
        pointLast = intrsctn->pos();
        polyline_current->points.push_back(pointLast);
        // Handle duplicate points and zero length segments.
        polyline_current->remove_duplicate_points();
        assert(! polyline_current->has_duplicate_points());
        // Handle nearly zero length edges.
        if (polyline_current->points.size() <= 1 ||
            (polyline_current->points.size() == 2 &&
                std::abs(polyline_current->points.front().x - polyline_current->points.back().x) < SCALED_EPSILON &&
                std::abs(polyline_current->points.front().y - polyline_current->points.back().y) < SCALED_EPSILON))
            polylines_out.pop_back();
        intrsctn = NULL;
        i_intersection = -1;
        polyline_current = NULL;
    }

#ifdef SLIC3R_DEBUG
    {
        static int iRun = 0;
        BoundingBox bbox_svg = poly_with_offset.bounding_box_outer();
        {
            ::Slic3r::SVG svg(debug_out_path("FillRectilinear2-final-%03d.svg", iRun), bbox_svg); // , scale_(1.));
            poly_with_offset.export_to_svg(svg);
            for (size_t i = n_polylines_out_initial; i < polylines_out.size(); ++ i)
                svg.draw(polylines_out[i].lines(), "black");
        }
        // Paint a picture per polyline. This makes it easier to discover the order of the polylines and their overlap.
        for (size_t i_polyline = n_polylines_out_initial; i_polyline < polylines_out.size(); ++ i_polyline) {
            ::Slic3r::SVG svg(debug_out_path("FillRectilinear2-final-%03d-%03d.svg", iRun, i_polyline), bbox_svg); // , scale_(1.));
            svg.draw(polylines_out[i_polyline].lines(), "black");
        }
    }
#endif /* SLIC3R_DEBUG */

    // paths must be rotated back
    for (Polylines::iterator it = polylines_out.begin() + n_polylines_out_initial; it != polylines_out.end(); ++ it) {
        // No need to translate, the absolute position is irrelevant.
        // it->translate(- rotate_vector.second.x, - rotate_vector.second.y);
        assert(! it->has_duplicate_points());
        //it->rotate(rotate_vector.first);
        //FIXME rather simplify the paths to avoid very short edges?
        //assert(! it->has_duplicate_points());
        it->remove_duplicate_points();
    }

#ifdef SLIC3R_DEBUG
    // Verify, that there are no duplicate points in the sequence.
    for (Polyline &polyline : polylines_out)
        assert(! polyline.has_duplicate_points());
#endif /* SLIC3R_DEBUG */

    return true;
}

}; // namespace FillRectilinear3_Internal

bool FillRectilinear3::fill_surface_by_lines(const Surface *surface, const FillParams &params, std::vector<FillDirParams> &fill_dir_params, Polylines &polylines_out)
{
    assert(params.density > 0.0001f && params.density <= 1.f);

    const float INFILL_OVERLAP_OVER_SPACING = 0.45f;
    assert(INFILL_OVERLAP_OVER_SPACING > 0 && INFILL_OVERLAP_OVER_SPACING < 0.5f);

    // On the polygons of poly_with_offset, the infill lines will be connected.
    FillRectilinear3_Internal::ExPolygonWithOffset poly_with_offset(
        surface->expolygon,
        float(scale_(- (0.5 - INFILL_OVERLAP_OVER_SPACING) * this->spacing)),
        float(scale_(- 0.5 * this->spacing)));
    if (poly_with_offset.n_contours_inner == 0) {
        // Not a single infill line fits.
        //FIXME maybe one shall trigger the gap fill here?
        return true;
    }

    // Rotate polygons so that we can work with vertical lines here
    std::pair<float, Point> rotate_vector = this->_infill_direction(surface);
    std::vector<FillRectilinear3_Internal::InfillHatchingSingleDirection> hatching(fill_dir_params.size(), FillRectilinear3_Internal::InfillHatchingSingleDirection());
    for (size_t i = 0; i < hatching.size(); ++ i)
        if (! FillRectilinear3_Internal::prepare_infill_hatching_segments(poly_with_offset, params, fill_dir_params[i], rotate_vector, hatching[i]))
            return false;

    for (size_t i = 0; i < hatching.size(); ++ i)
        if (! FillRectilinear3_Internal::fill_hatching_segments_legacy(
                poly_with_offset,
                params,
                this->link_max_length,
                hatching[i],
                polylines_out))
            return false;

    return true;
}

Polylines FillRectilinear3::fill_surface(const Surface *surface, const FillParams &params)
{
    Polylines polylines_out;
    std::vector<FillDirParams> fill_dir_params;
    fill_dir_params.emplace_back(FillDirParams(this->spacing, 0.f));
    if (! fill_surface_by_lines(surface, params, fill_dir_params, polylines_out))
        printf("FillRectilinear3::fill_surface() failed to fill a region.\n");
    if (params.full_infill() && ! params.dont_adjust)
        // Return back the adjusted spacing.
        this->spacing = fill_dir_params.front().spacing;
    return polylines_out;
}

Polylines FillGrid3::fill_surface(const Surface *surface, const FillParams &params)
{
    // Each linear fill covers half of the target coverage.
    FillParams params2 = params;
    params2.density *= 0.5f;
    Polylines polylines_out;
    std::vector<FillDirParams> fill_dir_params;
    fill_dir_params.emplace_back(FillDirParams(this->spacing, 0.f));
    fill_dir_params.emplace_back(FillDirParams(this->spacing, float(M_PI / 2.)));
    if (! fill_surface_by_lines(surface, params2, fill_dir_params, polylines_out))
        printf("FillGrid3::fill_surface() failed to fill a region.\n");
    return polylines_out;
}

Polylines FillTriangles3::fill_surface(const Surface *surface, const FillParams &params)
{
    // Each linear fill covers 1/3 of the target coverage.
    FillParams params2 = params;
    params2.density *= 0.333333333f;
    Polylines polylines_out;
    std::vector<FillDirParams> fill_dir_params;
    fill_dir_params.emplace_back(FillDirParams(this->spacing, 0.));
    fill_dir_params.emplace_back(FillDirParams(this->spacing, M_PI / 3.));
    fill_dir_params.emplace_back(FillDirParams(this->spacing, 2. * M_PI / 3.));
    if (! fill_surface_by_lines(surface, params2, fill_dir_params, polylines_out))
        printf("FillTriangles3::fill_surface() failed to fill a region.\n");
    return polylines_out;
}

Polylines FillStars3::fill_surface(const Surface *surface, const FillParams &params)
{
    // Each linear fill covers 1/3 of the target coverage.
    FillParams params2 = params;
    params2.density *= 0.333333333f;
    Polylines polylines_out;
    std::vector<FillDirParams> fill_dir_params;
    fill_dir_params.emplace_back(FillDirParams(this->spacing, 0.));
    fill_dir_params.emplace_back(FillDirParams(this->spacing, M_PI / 3.));
    fill_dir_params.emplace_back(FillDirParams(this->spacing, 2. * M_PI / 3., 0.5 * this->spacing / params2.density));
    if (! fill_surface_by_lines(surface, params2, fill_dir_params, polylines_out))
        printf("FillStars3::fill_surface() failed to fill a region.\n");
    return polylines_out;
}

Polylines FillCubic3::fill_surface(const Surface *surface, const FillParams &params)
{
    // Each linear fill covers 1/3 of the target coverage.
    FillParams params2 = params;
    params2.density *= 0.333333333f;
    Polylines polylines_out;
    std::vector<FillDirParams> fill_dir_params;
    coordf_t dx = sqrt(0.5) * z;
    fill_dir_params.emplace_back(FillDirParams(this->spacing, 0.,             dx));
    fill_dir_params.emplace_back(FillDirParams(this->spacing, M_PI / 3.,     -dx));
    fill_dir_params.emplace_back(FillDirParams(this->spacing, 2. * M_PI / 3., dx));
    if (! fill_surface_by_lines(surface, params2, fill_dir_params, polylines_out))
        printf("FillCubic3::fill_surface() failed to fill a region.\n");
    return polylines_out;
}

} // namespace Slic3r
