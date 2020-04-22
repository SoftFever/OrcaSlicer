#include <stdlib.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include <boost/container/small_vector.hpp>
#include <boost/static_assert.hpp>

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Geometry.hpp"
#include "../Surface.hpp"

#include "FillRectilinear2.hpp"

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #undef NDEBUG
    #include "SVG.hpp"
#endif

#include <cassert>

// We want our version of assert.
#include "../libslic3r.h"

namespace Slic3r {

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
        if (pa(0) > pb(0))
            std::swap(pa(0), pb(0));
        if (pa(1) > pb(1))
            std::swap(pa(1), pb(1));
        assert(px(0) >= pa(0) && px(0) <= pb(0));
        assert(px(1) >= pa(1) && px(1) <= pb(1));
    }
#endif /* SLIC3R_DEBUG */
    const Point *pPrev = &p1;
    const Point *pThis = NULL;
    coordf_t len = 0;
    if (seg1 <= seg2) {
        for (size_t i = seg1; i < seg2; ++ i, pPrev = pThis)
           len += (*pPrev - *(pThis = &poly.points[i])).cast<double>().norm();
    } else {
        for (size_t i = seg1; i < poly.points.size(); ++ i, pPrev = pThis)
           len += (*pPrev - *(pThis = &poly.points[i])).cast<double>().norm();
        for (size_t i = 0; i < seg2; ++ i, pPrev = pThis)
           len += (*pPrev - *(pThis = &poly.points[i])).cast<double>().norm();
    }
    len += (*pPrev - p2).cast<double>().norm();
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

// Intersection point of a vertical line with a polygon segment.
struct SegmentIntersection
{
    // Index of a contour in ExPolygonWithOffset, with which this vertical line intersects.
    size_t      iContour { 0 };
    // Index of a segment in iContour, with which this vertical line intersects.
    size_t      iSegment { 0 };
    // y position of the intersection, rational number.
    int64_t     pos_p { 0 };
    uint32_t    pos_q { 1 };

    coord_t     pos() const {
        // Division rounds both positive and negative down to zero.
        // Add half of q for an arithmetic rounding effect.
        int64_t p = pos_p;
        if (p < 0)
            p -= int64_t(pos_q>>1);
        else
            p += int64_t(pos_q>>1);
        return coord_t(p / int64_t(pos_q)); 
    }

    // Kind of intersection. With the original contour, or with the inner offestted contour?
    // A vertical segment will be at least intersected by OUTER_LOW, OUTER_HIGH,
    // but it could be intersected with OUTER_LOW, INNER_LOW, INNER_HIGH, OUTER_HIGH,
    // and there may be more than one pair of INNER_LOW, INNER_HIGH between OUTER_LOW, OUTER_HIGH.
    enum SegmentIntersectionType : char {
        OUTER_LOW   = 0,
        OUTER_HIGH  = 1,
        INNER_LOW   = 2,
        INNER_HIGH  = 3,
        UNKNOWN     = -1
    };
    SegmentIntersectionType type { UNKNOWN };

    // Left vertical line / contour intersection point.
    // null if next_on_contour_vertical.
    int32_t	prev_on_contour { 0 };
    // Right vertical line / contour intersection point.
    // If next_on_contour_vertical, then then next_on_contour contains next contour point on the same vertical line.
    int32_t	next_on_contour { 0 };

    enum class LinkType : uint8_t {
    	// Horizontal link (left or right).
    	Horizontal,
    	// Vertical link, up.
    	Up,
    	// Vertical link, down.
    	Down
    };

    enum class LinkQuality : uint8_t {
    	Invalid,
        Valid,
    	// Valid link, to be followed when extruding.
    	// Link inside a monotonous region.
    	ValidMonotonous,
    	// Valid link, to be possibly followed when extruding.
    	// Link between two monotonous regions.
    	ValidNonMonotonous,
    	// Link from T to end of another contour.
    	FromT,
    	// Link from end of one contour to T.
    	ToT,
    	// Link from one T to another T, making a letter H.
    	H,
    	// Vertical segment
    	TooLong,
    };

    // Kept grouped with other booleans for smaller memory footprint.
    LinkType 		prev_on_contour_type { LinkType::Horizontal };
    LinkType 		next_on_contour_type { LinkType::Horizontal };
    LinkQuality 	prev_on_contour_quality { true };
    LinkQuality 	next_on_contour_quality { true };
    // Was this segment along the y axis consumed?
    // Up means up along the vertical segment.
    bool 	 		consumed_vertical_up { false };
    // Was a segment of the inner perimeter contour consumed?
    // Right means right from the vertical segment.
    bool 	 		consumed_perimeter_right { false };

    // For the INNER_LOW type, this point may be connected to another INNER_LOW point following a perimeter contour.
    // For the INNER_HIGH type, this point may be connected to another INNER_HIGH point following a perimeter contour.
    // If INNER_LOW is connected to INNER_HIGH or vice versa,
    // one has to make sure the vertical infill line does not overlap with the connecting perimeter line.
    bool 	is_inner() const { return type == INNER_LOW  || type == INNER_HIGH; }
    bool 	is_outer() const { return type == OUTER_LOW  || type == OUTER_HIGH; }
    bool 	is_low  () const { return type == INNER_LOW  || type == OUTER_LOW; }
    bool 	is_high () const { return type == INNER_HIGH || type == OUTER_HIGH; }

    enum class Side {
    	Left,
    	Right
    };
    enum class Direction {
    	Up,
    	Down
    };

    bool 	has_left_horizontal()    		 	const { return this->prev_on_contour_type == LinkType::Horizontal; }
    bool 	has_right_horizontal()   		 	const { return this->next_on_contour_type == LinkType::Horizontal; }
    bool 	has_horizontal(Side side)		 	const { return side == Side::Left ? this->has_left_horizontal() : this->has_right_horizontal(); }

    bool 	has_left_vertical_up()   		 	const { return this->prev_on_contour_type == LinkType::Up; }
    bool 	has_left_vertical_down() 		 	const { return this->prev_on_contour_type == LinkType::Down; }
    bool 	has_left_vertical(Direction dir) 	const { return dir == Direction::Up ? this->has_left_vertical_up() : this->has_left_vertical_down(); }
    bool 	has_left_vertical()    	 		 	const { return this->has_left_vertical_up() || this->has_left_vertical_down(); }
    bool 	has_left_vertical_outside()			const { return this->is_low() ? this->has_left_vertical_down() : this->has_left_vertical_up(); }

    bool 	has_right_vertical_up()   			const { return this->next_on_contour_type == LinkType::Up; }
    bool 	has_right_vertical_down() 			const { return this->next_on_contour_type == LinkType::Down; }
    bool 	has_right_vertical(Direction dir) 	const { return dir == Direction::Up ? this->has_right_vertical_up() : this->has_right_vertical_down(); }
    bool 	has_right_vertical()    			const { return this->has_right_vertical_up() || this->has_right_vertical_down(); }
    bool 	has_right_vertical_outside()		const { return this->is_low() ? this->has_right_vertical_down() : this->has_right_vertical_up(); }

    bool 	has_vertical()						const { return this->has_left_vertical() || this->has_right_vertical(); }
    bool 	has_vertical(Side side)				const { return side == Side::Left ? this->has_left_vertical() : this->has_right_vertical(); }
    bool 	has_vertical_up()					const { return this->has_left_vertical_up() || this->has_right_vertical_up(); }
    bool 	has_vertical_down()					const { return this->has_left_vertical_down() || this->has_right_vertical_down(); }
    bool 	has_vertical(Direction dir)			const { return dir == Direction::Up ? this->has_vertical_up() : this->has_vertical_down(); }

    int 	left_horizontal()  					const { return this->has_left_horizontal() 	? this->prev_on_contour : -1; }
    int 	right_horizontal()  				const { return this->has_right_horizontal() ? this->next_on_contour : -1; }
    int 	horizontal(Side side)  				const { return side == Side::Left ? this->left_horizontal() : this->right_horizontal(); }

    int 	left_vertical_up()   		 		const { return this->has_left_vertical_up()    ? this->prev_on_contour : -1; }
    int 	left_vertical_down()   		 		const { return this->has_left_vertical_down()  ? this->prev_on_contour : -1; }
    int 	left_vertical(Direction dir) 		const { return (dir == Direction::Up ? this->has_left_vertical_up() : this->has_left_vertical_down()) ? this->prev_on_contour : -1; }
    int 	left_vertical()   			 		const { return this->has_left_vertical() 	   ? this->prev_on_contour : -1; }
    int 	left_vertical_outside()				const { return this->is_low() ? this->left_vertical_down() : this->left_vertical_up(); }
    int 	right_vertical_up()   		 		const { return this->has_right_vertical_up()   ? this->prev_on_contour : -1; }
    int 	right_vertical_down()   	 		const { return this->has_right_vertical_down() ? this->prev_on_contour : -1; }
    int 	right_vertical(Direction dir) 		const { return (dir == Direction::Up ? this->has_right_vertical_up() : this->has_right_vertical_down()) ? this->next_on_contour : -1; }
    int 	right_vertical()   			 		const { return this->has_right_vertical() 	   ? this->prev_on_contour : -1; }
    int 	right_vertical_outside()			const { return this->is_low() ? this->right_vertical_down() : this->right_vertical_up(); }

    int 	vertical_up(Side side)				const { return side == Side::Left ? this->left_vertical_up() : this->right_vertical_up(); }
    int 	vertical_down(Side side)			const { return side == Side::Left ? this->left_vertical_down() : this->right_vertical_down(); }
    int 	vertical_outside(Side side)			const { return side == Side::Left ? this->left_vertical_outside() : this->right_vertical_outside(); }
    int 	vertical_up()						const { 
    	assert(! this->has_left_vertical_up() || ! this->has_right_vertical_up());
    	return this->has_left_vertical_up() ? this->left_vertical_up() : this->right_vertical_up();
    }
    LinkQuality vertical_up_quality()			const {
    	assert(! this->has_left_vertical_up() || ! this->has_right_vertical_up());
    	return this->has_left_vertical_up() ? this->prev_on_contour_quality : this->next_on_contour_quality;
    }
    int 	vertical_down()						const {
    	assert(! this->has_left_vertical_down() || ! this->has_right_vertical_down());
    	return this->has_left_vertical_down() ? this->left_vertical_down() : this->right_vertical_down();
    }
    LinkQuality vertical_down_quality()			const {
    	assert(! this->has_left_vertical_down() || ! this->has_right_vertical_down());
    	return this->has_left_vertical_down() ? this->prev_on_contour_quality : this->next_on_contour_quality;
    }
    int 	vertical_outside()					const { return this->is_low() ? this->vertical_down() : this->vertical_up(); }

//    int  	next_up()    const { return this->prev_on_contour_vertical ? -1 : this->prev_on_contour; }
//    int  	next_right() const { return this->next_on_contour_vertical ? -1 : this->next_on_contour; }

    // Compare two y intersection points given by rational numbers.
    // Note that the rational number is given as pos_p/pos_q, where pos_p is int64 and pos_q is uint32.
    // This function calculates pos_p * other.pos_q < other.pos_p * pos_q as a 48bit number.
    // We don't use 128bit intrinsic data types as these are usually not supported by 32bit compilers and
    // we don't need the full 128bit precision anyway.
    bool operator<(const SegmentIntersection &other) const
    {
        assert(pos_q > 0);
        assert(other.pos_q > 0);
        if (pos_p == 0 || other.pos_p == 0) {
            // Because the denominators are positive and one of the nominators is zero,
            // following simple statement holds.
            return pos_p < other.pos_p;
        } else {
            // None of the nominators is zero.
            int sign1 = (pos_p > 0) ? 1 : -1;
            int sign2 = (other.pos_p > 0) ? 1 : -1;
            int signs = sign1 * sign2;
            assert(signs == 1 || signs == -1);
            if (signs < 0) {
                // The nominators have different signs.
                return sign1 < 0;
            } else {
                // The nominators have the same sign.
                // Absolute values
                uint64_t p1, p2;
                if (sign1 > 0) {
                    p1 = uint64_t(pos_p);
                    p2 = uint64_t(other.pos_p);
                } else {
                    p1 = uint64_t(- pos_p);
                    p2 = uint64_t(- other.pos_p);
                };
                // Multiply low and high 32bit words of p1 by other_pos.q
                // 32bit x 32bit => 64bit
                // l_hi and l_lo overlap by 32 bits.
                uint64_t l_hi = (p1 >> 32) * uint64_t(other.pos_q);
                uint64_t l_lo = (p1 & 0xffffffffll) * uint64_t(other.pos_q);
                l_hi += (l_lo >> 32);
                uint64_t r_hi = (p2 >> 32) * uint64_t(pos_q);
                uint64_t r_lo = (p2 & 0xffffffffll) * uint64_t(pos_q);
                r_hi += (r_lo >> 32);
                // Compare the high 64 bits.
                if (l_hi == r_hi) {
                    // Compare the low 32 bits.
                    l_lo &= 0xffffffffll;
                    r_lo &= 0xffffffffll;
                    return (sign1 < 0) ? (l_lo > r_lo) : (l_lo < r_lo);
                }
                return (sign1 < 0) ? (l_hi > r_hi) : (l_hi < r_hi);
            }
        }
    }

    bool operator==(const SegmentIntersection &other) const 
    {
        assert(pos_q > 0);
        assert(other.pos_q > 0);
        if (pos_p == 0 || other.pos_p == 0) {
            // Because the denominators are positive and one of the nominators is zero,
            // following simple statement holds.
            return pos_p == other.pos_p;
        }

        // None of the nominators is zero, none of the denominators is zero.
        bool positive = pos_p > 0;
        if (positive != (other.pos_p > 0))
            return false;
        // The nominators have the same sign.
        // Absolute values
        uint64_t p1 = positive ? uint64_t(pos_p) : uint64_t(- pos_p);
        uint64_t p2 = positive ? uint64_t(other.pos_p) : uint64_t(- other.pos_p);
        // Multiply low and high 32bit words of p1 by other_pos.q
        // 32bit x 32bit => 64bit
        // l_hi and l_lo overlap by 32 bits.
        uint64_t l_lo = (p1 & 0xffffffffll) * uint64_t(other.pos_q);
        uint64_t r_lo = (p2 & 0xffffffffll) * uint64_t(pos_q);
        if (l_lo != r_lo)
            return false;
        uint64_t l_hi = (p1 >> 32) * uint64_t(other.pos_q);
        uint64_t r_hi = (p2 >> 32) * uint64_t(pos_q);
        return l_hi + (l_lo >> 32) == r_hi + (r_lo >> 32);
    }
};
static_assert(sizeof(SegmentIntersection::pos_q) == 4, "SegmentIntersection::pos_q has to be 32bit long!");

// A vertical line with intersection points with polygons.
struct SegmentedIntersectionLine
{
    // Index of this vertical intersection line.
    size_t                              idx;
    // x position of this vertical intersection line.
    coord_t                             pos;
    // List of intersection points with polygons, sorted increasingly by the y axis.
    std::vector<SegmentIntersection>    intersections;
};

// A container maintaining an expolygon with its inner offsetted polygon.
// The purpose of the inner offsetted polygon is to provide segments to connect the infill lines.
struct ExPolygonWithOffset
{
public:
    ExPolygonWithOffset(
        const ExPolygon &expolygon,
        float   angle,
        coord_t aoffset1,
        coord_t aoffset2)
    {
        // Copy and rotate the source polygons.
        polygons_src = expolygon;
        polygons_src.contour.rotate(angle);
        for (Polygons::iterator it = polygons_src.holes.begin(); it != polygons_src.holes.end(); ++ it)
            it->rotate(angle);

        double mitterLimit = 3.;
        // for the infill pattern, don't cut the corners.
        // default miterLimt = 3
        //double mitterLimit = 10.;
        assert(aoffset1 < 0);
        assert(aoffset2 < 0);
        assert(aoffset2 < aoffset1);
//        bool sticks_removed = 
        remove_sticks(polygons_src);
//        if (sticks_removed) printf("Sticks removed!\n");
        polygons_outer = offset(polygons_src, float(aoffset1),
            ClipperLib::jtMiter,
            mitterLimit);
        polygons_inner = offset(polygons_outer, float(aoffset2 - aoffset1),
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

    bool             is_contour_ccw(size_t idx) const { return polygons_ccw[idx]; }

    BoundingBox      bounding_box_src() const 
        { return get_extents(polygons_src); }
    BoundingBox      bounding_box_outer() const 
        { return get_extents(polygons_outer); }
    BoundingBox      bounding_box_inner() const 
        { return get_extents(polygons_inner); }

#ifdef SLIC3R_DEBUG
    void export_to_svg(Slic3r::SVG &svg) {
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

static inline int distance_of_segmens(const Polygon &poly, size_t seg1, size_t seg2, bool forward)
{
    int d = int(seg2) - int(seg1);
    if (! forward)
        d = - d;
    if (d < 0)
        d += int(poly.points.size());
    return d;
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
    SegmentIntersection::Side                      side)
{
    const SegmentedIntersectionLine &il_this      = segs[iVerticalLine];
    const SegmentIntersection       &itsct_this   = il_this.intersections[iIntersection];
	if (itsct_this.has_vertical(side))
	    // Not the first intersection along the contor. This intersection point
	    // has been preceded by an intersection point along the vertical line.
		return INTERSECTION_TYPE_OTHER_VLINE_NOT_FIRST;
    int iIntersectionOther = itsct_this.horizontal(side);
    if (iIntersectionOther == -1)
        return INTERSECTION_TYPE_OTHER_VLINE_UNDEFINED;
    assert(side == SegmentIntersection::Side::Right ? (iVerticalLine + 1 < segs.size()) : (iVerticalLine > 0));
    const SegmentedIntersectionLine &il_other     = segs[side == SegmentIntersection::Side::Right ? (iVerticalLine+1) : (iVerticalLine-1)];
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
    if (side == SegmentIntersection::Side::Right ? itsct_this.consumed_perimeter_right : itsct_other.consumed_perimeter_right)
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
    size_t                                         iIntersection)
{
    return intersection_type_on_prev_next_vertical_line(segs, iVerticalLine, iIntersection, SegmentIntersection::Side::Left);
}

static inline IntersectionTypeOtherVLine intersection_type_on_next_vertical_line(
    const std::vector<SegmentedIntersectionLine>  &segs, 
    size_t                                         iVerticalLine, 
    size_t                                         iIntersection)
{
    return intersection_type_on_prev_next_vertical_line(segs, iVerticalLine, iIntersection, SegmentIntersection::Side::Right);
}

// Measure an Euclidian length of a perimeter segment when going from iIntersection to iIntersection2.
static inline coordf_t measure_perimeter_prev_next_segment_length(
    const ExPolygonWithOffset                     &poly_with_offset, 
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
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
    assert(itsct.iContour == itsct2.iContour);
    const Polygon                   &poly   = poly_with_offset.contour(itsct.iContour);
//    const bool                       ccw    = poly_with_offset.is_contour_ccw(il.iContour);
    assert(itsct.type == itsct2.type);
    assert(itsct.iContour == itsct2.iContour);
    assert(itsct.is_inner());
    const bool                       forward = itsct.is_low() == dir_is_next;

    Point p1(il.pos, itsct.pos());
    Point p2(il2.pos, itsct2.pos());
    return forward ?
        segment_length(poly, itsct .iSegment, p1, itsct2.iSegment, p2) :
        segment_length(poly, itsct2.iSegment, p2, itsct .iSegment, p1);
}

static inline coordf_t measure_perimeter_prev_segment_length(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iIntersection,
    size_t                                         iIntersection2)
{
    return measure_perimeter_prev_next_segment_length(poly_with_offset, segs, iVerticalLine, iIntersection, iIntersection2, false);
}

static inline coordf_t measure_perimeter_next_segment_length(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iIntersection,
    size_t                                         iIntersection2)
{
    return measure_perimeter_prev_next_segment_length(poly_with_offset, segs, iVerticalLine, iIntersection, iIntersection2, true);
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
    out.points.push_back(Point(il2.pos, itsct2.pos()));
}

static inline coordf_t measure_perimeter_segment_on_vertical_line_length(
    const ExPolygonWithOffset                     &poly_with_offset,
    const std::vector<SegmentedIntersectionLine>  &segs,
    size_t                                         iVerticalLine,
    size_t                                         iIntersection,
    size_t                                         iIntersection2,
    bool                                           forward)
{
    const SegmentedIntersectionLine &il = segs[iVerticalLine];
    const SegmentIntersection       &itsct = il.intersections[iIntersection];
    const SegmentIntersection       &itsct2 = il.intersections[iIntersection2];
    const Polygon                   &poly = poly_with_offset.contour(itsct.iContour);
    assert(itsct.is_inner());
    assert(itsct2.is_inner());
    assert(itsct.type != itsct2.type);
    assert(itsct.iContour == itsct2.iContour);
    Point p1(il.pos, itsct.pos());
    Point p2(il.pos, itsct2.pos());
    return forward ?
        segment_length(poly, itsct .iSegment, p1, itsct2.iSegment, p2) :
        segment_length(poly, itsct2.iSegment, p2, itsct .iSegment, p1);
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
	out.points.push_back(Point(il.pos, itsct2.pos()));
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

static std::vector<SegmentedIntersectionLine> slice_region_by_vertical_lines(const ExPolygonWithOffset &poly_with_offset, size_t n_vlines, coord_t x0, coord_t line_spacing)
{
    // Allocate storage for the segments.
    std::vector<SegmentedIntersectionLine> segs(n_vlines, SegmentedIntersectionLine());
    for (coord_t i = 0; i < coord_t(n_vlines); ++ i) {
        segs[i].idx = i;
        segs[i].pos = x0 + i * line_spacing;
    }
    // For each contour
    for (size_t iContour = 0; iContour < poly_with_offset.n_contours; ++ iContour) {
        const Points &contour = poly_with_offset.contour(iContour).points;
        if (contour.size() < 2)
            continue;
        // For each segment
        for (size_t iSegment = 0; iSegment < contour.size(); ++ iSegment) {
            size_t iPrev = ((iSegment == 0) ? contour.size() : iSegment) - 1;
            const Point &p1 = contour[iPrev];
            const Point &p2 = contour[iSegment];
            // Which of the equally spaced vertical lines is intersected by this segment?
            coord_t l = p1(0);
            coord_t r = p2(0);
            if (l > r)
                std::swap(l, r);
            // il, ir are the left / right indices of vertical lines intersecting a segment
            int il = (l - x0) / line_spacing;
            while (il * line_spacing + x0 < l)
                ++ il;
            il = std::max(int(0), il);
            int ir = (r - x0 + line_spacing) / line_spacing;
            while (ir * line_spacing + x0 > r)
                -- ir;
            ir = std::min(int(segs.size()) - 1, ir);
            if (il > ir)
                // No vertical line intersects this segment.
                continue;
            assert(il >= 0 && size_t(il) < segs.size());
            assert(ir >= 0 && size_t(ir) < segs.size());
            for (int i = il; i <= ir; ++ i) {
                coord_t this_x = segs[i].pos;
				assert(this_x == i * line_spacing + x0);
                SegmentIntersection is;
                is.iContour = iContour;
                is.iSegment = iSegment;
                assert(l <= this_x);
                assert(r >= this_x);
                // Calculate the intersection position in y axis. x is known.
                if (p1(0) == this_x) {
                    if (p2(0) == this_x) {
                        // Ignore strictly vertical segments.
                        continue;
                    }
                    is.pos_p = p1(1);
                    is.pos_q = 1;
                } else if (p2(0) == this_x) {
                    is.pos_p = p2(1);
                    is.pos_q = 1;
                } else {
                    // First calculate the intersection parameter 't' as a rational number with non negative denominator.
                    if (p2(0) > p1(0)) {
                        is.pos_p = this_x - p1(0);
                        is.pos_q = p2(0) - p1(0);
                    } else {
                        is.pos_p = p1(0) - this_x;
                        is.pos_q = p1(0) - p2(0);
                    }
                    assert(is.pos_p >= 0 && is.pos_p <= is.pos_q);
                    // Make an intersection point from the 't'.
                    is.pos_p *= int64_t(p2(1) - p1(1));
                    is.pos_p += p1(1) * int64_t(is.pos_q);
                }
                // +-1 to take rounding into account.
                assert(is.pos() + 1 >= std::min(p1(1), p2(1)));
                assert(is.pos() <= std::max(p1(1), p2(1)) + 1);
                segs[i].intersections.push_back(is);
            }
        }
    }

    // Sort the intersections along their segments, specify the intersection types.
    for (size_t i_seg = 0; i_seg < segs.size(); ++ i_seg) {
        SegmentedIntersectionLine &sil = segs[i_seg];
        // Sort the intersection points using exact rational arithmetic.
        std::sort(sil.intersections.begin(), sil.intersections.end());
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
            coord_t dir = contour[iSegment](0) - contour[iPrev](0);
            bool low = dir > 0;
            sil.intersections[i].type = poly_with_offset.is_contour_outer(iContour) ? 
                (low ? SegmentIntersection::OUTER_LOW : SegmentIntersection::OUTER_HIGH) :
                (low ? SegmentIntersection::INNER_LOW : SegmentIntersection::INNER_HIGH);
            if (j > 0 && sil.intersections[i].iContour == sil.intersections[j-1].iContour) {
                // Two successive intersection points on a vertical line with the same contour. This may be a special case.
                if (sil.intersections[i].pos() == sil.intersections[j-1].pos()) {
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
#define ASSERT_THROW(CONDITION) do { assert(CONDITION); throw InfillFailedException(); } while (0)
    for (size_t i_seg = 0; i_seg < segs.size(); ++ i_seg) {
        SegmentedIntersectionLine &sil = segs[i_seg];
        // The intersection points have to be even.
        ASSERT_THROW((sil.intersections.size() & 1) == 0);
        for (size_t i = 0; i < sil.intersections.size();) {
            // An intersection segment crossing the bigger contour may cross the inner offsetted contour even number of times.
            ASSERT_THROW(sil.intersections[i].type == SegmentIntersection::OUTER_LOW);
            size_t j = i + 1;
            ASSERT_THROW(j < sil.intersections.size());
            ASSERT_THROW(sil.intersections[j].type == SegmentIntersection::INNER_LOW || sil.intersections[j].type == SegmentIntersection::OUTER_HIGH);
            for (; j < sil.intersections.size() && sil.intersections[j].is_inner(); ++ j) ;
            ASSERT_THROW(j < sil.intersections.size());
            ASSERT_THROW((j & 1) == 1);
            ASSERT_THROW(sil.intersections[j].type == SegmentIntersection::OUTER_HIGH);
            ASSERT_THROW(i + 1 == j || sil.intersections[j - 1].type == SegmentIntersection::INNER_HIGH);
            i = j + 1;
        }
    }
#undef ASSERT_THROW

    return segs;
}

// Connect each contour / vertical line intersection point with another two contour / vertical line intersection points.
// (fill in SegmentIntersection::{prev_on_contour, prev_on_contour_vertical, next_on_contour, next_on_contour_vertical}.
// These contour points are either on the same vertical line, or on the vertical line left / right to the current one.
static void connect_segment_intersections_by_contours(const ExPolygonWithOffset &poly_with_offset, std::vector<SegmentedIntersectionLine> &segs)
{
    for (size_t i_vline = 0; i_vline < segs.size(); ++ i_vline) {
	    SegmentedIntersectionLine       &il      = segs[i_vline];
	    const SegmentedIntersectionLine *il_prev = i_vline > 0 ? &segs[i_vline - 1] : nullptr;
	    const SegmentedIntersectionLine *il_next = i_vline + 1 < segs.size() ? &segs[i_vline + 1] : nullptr;

        for (size_t i_intersection = 0; i_intersection + 1 < il.intersections.size(); ++ i_intersection) {
		    SegmentIntersection &itsct = il.intersections[i_intersection];
	        const Polygon 		&poly  = poly_with_offset.contour(itsct.iContour);

	        // 1) Find possible connection points on the previous / next vertical line.
		    // Find an intersection point on iVerticalLineOther, intersecting iInnerContour
		    // at the same orientation as iIntersection, and being closest to iIntersection
		    // in the number of contour segments, when following the direction of the contour.
		    int iprev = -1;
		    if (il_prev) {
			    int dmin = std::numeric_limits<int>::max();
			    for (size_t i = 0; i < il_prev->intersections.size(); ++ i) {
			        const SegmentIntersection &itsct2 = il_prev->intersections[i];
			        if (itsct.iContour == itsct2.iContour && itsct.type == itsct2.type) {
			            // The intersection points lie on the same contour and have the same orientation.
			            // Find the intersection point with a shortest path in the direction of the contour.
			            int d = distance_of_segmens(poly, itsct.iSegment, itsct2.iSegment, false);
			            if (d < dmin) {
			                iprev = i;
			                dmin  = d;
			            }
			        }
			    }
			}
		    int inext = -1;
		    if (il_next) {
			    int dmin = std::numeric_limits<int>::max();
			    for (size_t i = 0; i < il_next->intersections.size(); ++ i) {
			        const SegmentIntersection &itsct2 = il_next->intersections[i];
			        if (itsct.iContour == itsct2.iContour && itsct.type == itsct2.type) {
			            // The intersection points lie on the same contour and have the same orientation.
			            // Find the intersection point with a shortest path in the direction of the contour.
			            int d = distance_of_segmens(poly, itsct.iSegment, itsct2.iSegment, true);
			            if (d < dmin) {
			                inext = i;
			                dmin  = d;
			            }
			        }
			    }
			}

	        // 2) Find possible connection points on the same vertical line.
	        int iabove = -1;
            // Does the perimeter intersect the current vertical line above intrsctn?
            for (size_t i = i_intersection + 1; i + 1 < il.intersections.size(); ++ i)
                if (il.intersections[i].iContour == itsct.iContour) {
                    iabove = i;
                    break;
                }
            // Does the perimeter intersect the current vertical line below intrsctn?
	        int ibelow = -1;
            for (size_t i = i_intersection - 1; i > 0; -- i)
                if (il.intersections[i].iContour == itsct.iContour) {
                    ibelow = i;
                    break;
                }

	        // 3) Sort the intersection points, clear iprev / inext / iSegBelow / iSegAbove,
	        // if it is preceded by any other intersection point along the contour.
	        // The perimeter contour orientation.
	        const bool forward = itsct.is_low(); // == poly_with_offset.is_contour_ccw(intrsctn->iContour);
	        {
	            int d_horiz = (iprev  == -1) ? std::numeric_limits<int>::max() :
	                distance_of_segmens(poly, il_prev->intersections[iprev].iSegment, itsct.iSegment, forward);
	            int d_down  = (ibelow == -1) ? std::numeric_limits<int>::max() :
	                distance_of_segmens(poly, il.intersections[ibelow].iSegment, itsct.iSegment, forward);
	            int d_up    = (iabove == -1) ? std::numeric_limits<int>::max() :
	                distance_of_segmens(poly, il.intersections[ibelow].iSegment, itsct.iSegment, forward);
	            if (d_horiz < std::min(d_down, d_up)) {
                    itsct.prev_on_contour 	    = iprev;
                    itsct.prev_on_contour_type  = SegmentIntersection::LinkType::Horizontal;
	            } else if (d_down < d_up) {
                    itsct.prev_on_contour 		= ibelow;
                    itsct.prev_on_contour_type  = SegmentIntersection::LinkType::Down;
                } else {
                    itsct.prev_on_contour       = iabove;
                    itsct.prev_on_contour_type  = SegmentIntersection::LinkType::Up;
                }
	            // There should always be a link to the next intersection point on the same contour.
	            assert(itsct.prev_on_contour != -1);
	        }
	        {
	            int d_horiz = (inext  == -1) ? std::numeric_limits<int>::max() :
	                distance_of_segmens(poly, itsct.iSegment, il_next->intersections[inext].iSegment, forward);
	            int d_down  = (ibelow == -1) ? std::numeric_limits<int>::max() :
	                distance_of_segmens(poly, itsct.iSegment, il.intersections[ibelow].iSegment, forward);
	            int d_up    = (iabove == -1) ? std::numeric_limits<int>::max() :
	                distance_of_segmens(poly, itsct.iSegment, il.intersections[iabove].iSegment, forward);
	            if (d_horiz < std::min(d_down, d_up)) {
                    itsct.next_on_contour 	    = inext;
                    itsct.next_on_contour_type  = SegmentIntersection::LinkType::Horizontal;
                } else if (d_down < d_up) {
                    itsct.next_on_contour       = ibelow;
                    itsct.next_on_contour_type  = SegmentIntersection::LinkType::Down;
                } else {
                    itsct.next_on_contour       = iabove;
                    itsct.next_on_contour_type  = SegmentIntersection::LinkType::Up;
                }
	            // There should always be a link to the next intersection point on the same contour.
	            assert(itsct.next_on_contour != -1);
	        }
	    }
    }
}

// Find the last INNER_HIGH intersection starting with INNER_LOW, that is followed by OUTER_HIGH intersection.
// Such intersection shall always exist.
static const SegmentIntersection& end_of_vertical_run_raw(const SegmentIntersection &start)
{
	assert(start.type != SegmentIntersection::INNER_LOW);
    // Step back to the beginning of the vertical segment to mark it as consumed.
    auto *it = &start;
    do {
        ++ it;
    } while (it->type != SegmentIntersection::OUTER_HIGH);
    if ((it - 1)->is_inner()) {
        // Step back.
        -- it;
        assert(it->type == SegmentIntersection::INNER_HIGH);
    }
    return *it;
}
static SegmentIntersection& end_of_vertical_run_raw(SegmentIntersection &start)
{
	return const_cast<SegmentIntersection&>(end_of_vertical_run_raw(std::as_const(start)));
}

// Find the last INNER_HIGH intersection starting with INNER_LOW, that is followed by OUTER_HIGH intersection, traversing vertical up contours if enabled.
// Such intersection shall always exist.
static const SegmentIntersection& end_of_vertical_run(const SegmentedIntersectionLine &il, const SegmentIntersection &start)
{
	assert(start.type != SegmentIntersection::INNER_LOW);
	const SegmentIntersection *end = &end_of_vertical_run_raw(start);
	assert(end->type == SegmentIntersection::INNER_HIGH);
	for (;;) {
		int up = end->vertical_up();
		if (up == -1 || (end->has_left_vertical_up() ? end->prev_on_contour_quality : end->next_on_contour_quality) != SegmentIntersection::LinkQuality::Valid)
			break;
		const SegmentIntersection &new_start = il.intersections[up];
		assert(end->iContour == new_start.iContour);
		assert(new_start.type == SegmentIntersection::INNER_LOW);
		end = &end_of_vertical_run_raw(new_start);
	}
	assert(end->type == SegmentIntersection::INNER_HIGH);
	return *end;
}
static SegmentIntersection& end_of_vertical_run(SegmentedIntersectionLine &il, SegmentIntersection &start)
{
	return const_cast<SegmentIntersection&>(end_of_vertical_run(std::as_const(il), std::as_const(start)));
}

static void classify_vertical_runs(
	const ExPolygonWithOffset &poly_with_offset, const FillParams &params, const coord_t link_max_length, 
	std::vector<SegmentedIntersectionLine> &segs, size_t i_vline)
{
	SegmentedIntersectionLine &vline = segs[i_vline];
    for (size_t i_intersection = 0; i_intersection + 1 < vline.intersections.size(); ++ i_intersection) {
    	if (vline.intersections[i_intersection].type == SegmentIntersection::OUTER_LOW) {
    		if (vline.intersections[++ i_intersection].type == SegmentIntersection::INNER_LOW) {
    			for (;;) {
        			SegmentIntersection &start = vline.intersections[i_intersection];
			        SegmentIntersection &end   = end_of_vertical_run_raw(start);
			        SegmentIntersection::LinkQuality link_quality = SegmentIntersection::LinkQuality::Valid;
					// End of a contour starting at end and ending above end at the same vertical line.
					int inext = end.vertical_outside();
					if (inext == -1) {
						i_intersection = &end - vline.intersections.data() + 1;
						break;
					}
        			SegmentIntersection &start2 = vline.intersections[inext];
        			if (params.dont_connect)
        				link_quality = SegmentIntersection::LinkQuality::TooLong;
        			else {
                        for (SegmentIntersection *it = &end + 1; it != &start2; ++ it)
                            if (it->is_inner()) {
		        				link_quality = SegmentIntersection::LinkQuality::Invalid;
                                break;
                            }
        				if (link_quality == SegmentIntersection::LinkQuality::Valid && link_max_length > 0) {
                        	// Measure length of the link.
	                        coordf_t link_length = measure_perimeter_segment_on_vertical_line_length(
	                            poly_with_offset, segs, i_vline, i_intersection, inext, end.has_right_vertical_outside());
	                        if (link_length > link_max_length)
		        				link_quality = SegmentIntersection::LinkQuality::TooLong;
                        }
                    }
                    (end.has_left_vertical_up() ? end.prev_on_contour_quality : end.next_on_contour_quality) = link_quality;
                    (start2.has_left_vertical_down() ? start2.prev_on_contour_quality : start2.next_on_contour_quality) = link_quality;
                    if (link_quality != SegmentIntersection::LinkQuality::Valid) {
						i_intersection = &end - vline.intersections.data() + 1;
                    	break;
                    }
					i_intersection = &start2 - vline.intersections.data();
                }
    		} else
    			++ i_intersection;
    	} else
    		++ i_intersection;
    }	
}

static void classify_horizontal_links(
	const ExPolygonWithOffset &poly_with_offset, const FillParams &params, const coord_t link_max_length, 
	std::vector<SegmentedIntersectionLine> &segs, size_t i_vline)
{
	SegmentedIntersectionLine &vline_left  = segs[i_vline];
	SegmentedIntersectionLine &vline_right = segs[i_vline + 1];

	// Traverse both left and right together.
	size_t i_intersection_left  = 0;
	size_t i_intersection_right = 0;
	while (i_intersection_left + 1 < vline_left.intersections.size() && i_intersection_right + 1 < vline_right.intersections.size()) {
    	if (i_intersection_left < vline_left.intersections.size() && vline_left.intersections[i_intersection_left].type != SegmentIntersection::INNER_LOW) {
    		++ i_intersection_left;
    		continue;
    	}
    	if (i_intersection_right < vline_right.intersections.size() && vline_right.intersections[i_intersection_right].type != SegmentIntersection::INNER_LOW) {
    		++ i_intersection_right;
    		continue;
    	}

		if (i_intersection_left + 1 >= vline_left.intersections.size()) {
			// Trace right only.
		} else if (i_intersection_right + 1 >= vline_right.intersections.size()) {
			// Trace left only.
		} else {
			// Trace both.
			SegmentIntersection &start_left  = vline_left.intersections[i_intersection_left];
	        SegmentIntersection &end_left    = end_of_vertical_run(vline_left, start_left);
			SegmentIntersection &start_right = vline_right.intersections[i_intersection_right];
	        SegmentIntersection &end_right   = end_of_vertical_run(vline_right, start_right);
	        // Do these runs overlap?
	        int                    end_right_horizontal = end_left.right_horizontal();
	        int                    end_left_horizontal  = end_right.left_horizontal();
	        if (end_right_horizontal != -1) {
	        	if (end_right_horizontal < &start_right - vline_right.intersections.data()) {
	        		// Left precedes the right segment.
	        	}
	        } else if (end_left_horizontal != -1) {
	        	if (end_left_horizontal < &start_left - vline_left.intersections.data()) {
	        		// Right precedes the left segment.
	        	}
	        }
		}
	}

#if 0
    for (size_t i_intersection = 0; i_intersection + 1 < seg.intersections.size(); ++ i_intersection) {
    	if (segs.intersections[i_intersection].type == SegmentIntersection::OUTER_LOW) {
    		if (segs.intersections[++ i_intersection].type == SegmentIntersection::INNER_LOW) {
    			for (;;) {
        			SegmentIntersection &start = segs.intersections[i_intersection];
			        SegmentIntersection &end   = end_of_vertical_run_raw(start);
			        SegmentIntersection::LinkQuality link_quality = SegmentIntersection::LinkQuality::Valid;
					// End of a contour starting at end and ending above end at the same vertical line.
					int inext = end.vertical_outside();
					if (inext == -1) {
						i_intersection = &end - segs.intersections.data() + 1;
						break;
					}
        			SegmentIntersection &start2 = segs.intersections[inext];
        			if (params.dont_connect)
        				link_quality = SegmentIntersection::LinkQuality::TooLong;
        			else {
                        for (SegmentIntersection *it = &end + 1; it != &start2; ++ it)
                            if (it->is_inner()) {
		        				link_quality = SegmentIntersection::LinkQuality::Invalid;
                                break;
                            }
        				if (link_quality == SegmentIntersection::LinkQuality::Valid && link_max_length > 0) {
                        	// Measure length of the link.
	                        coordf_t link_length = measure_perimeter_segment_on_vertical_line_length(
	                            poly_with_offset, segs, i_vline, i_intersection, inext, intrsctn->has_right_vertical_outside());
	                        if (link_length > link_max_length)
		        				link_quality = SegmentIntersection::LinkQuality::TooLong;
                        }
                    }
                    (end.has_left_vertical_up() ? end.prev_on_contour_quality : end.next_on_contour_quality) = link_quality;
                    (start2.has_left_vertical_down() ? start2.prev_on_contour_quality : start2.next_on_contour_quality) = link_quality;
                    if (link_quality != SegmentIntersection::LinkQuality::Valid) {
						i_intersection = &end - segs.intersections.data() + 1;
                    	break;
                    }
					i_intersection = &start2 - segs.intersections.data();
                }
    		} else
    			++ i_intersection;
    	} else
    		++ i_intersection;
    }
#endif
}

static void disconnect_invalid_contour_links(
	const ExPolygonWithOffset& poly_with_offset, const FillParams& params, const coord_t link_max_length, std::vector<SegmentedIntersectionLine>& segs)
{
	// Make the links symmetric!

	// Validate vertical runs including vertical contour links.
    for (size_t i_vline = 0; i_vline < segs.size(); ++ i_vline) {
		classify_vertical_runs(poly_with_offset, params, link_max_length, segs, i_vline);
		if (i_vline > 0)
			classify_horizontal_links(poly_with_offset, params, link_max_length, segs, i_vline - 1);
    }
}

static void traverse_graph_generate_polylines(
	const ExPolygonWithOffset& poly_with_offset, const FillParams& params, const coord_t link_max_length, std::vector<SegmentedIntersectionLine>& segs, Polylines& polylines_out)
{
    // For each outer only chords, measure their maximum distance to the bow of the outer contour.
    // Mark an outer only chord as consumed, if the distance is low.
    for (size_t i_vline = 0; i_vline < segs.size(); ++i_vline) {
        SegmentedIntersectionLine& seg = segs[i_vline];
        for (size_t i_intersection = 0; i_intersection + 1 < seg.intersections.size(); ++i_intersection) {
            if (seg.intersections[i_intersection].type == SegmentIntersection::OUTER_LOW &&
                seg.intersections[i_intersection + 1].type == SegmentIntersection::OUTER_HIGH) {
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
    Polyline* polyline_current = NULL;
    if (!polylines_out.empty())
        pointLast = polylines_out.back().points.back();
    for (;;) {
        if (i_intersection == size_t(-1)) {
            // The path has been interrupted. Find a next starting point, closest to the previous extruder position.
            coordf_t dist2min = std::numeric_limits<coordf_t>().max();
            for (size_t i_vline2 = 0; i_vline2 < segs.size(); ++i_vline2) {
                const SegmentedIntersectionLine& seg = segs[i_vline2];
                if (!seg.intersections.empty()) {
                    assert(seg.intersections.size() > 1);
                    // Even number of intersections with the loops.
                    assert((seg.intersections.size() & 1) == 0);
                    assert(seg.intersections.front().type == SegmentIntersection::OUTER_LOW);
                    for (size_t i = 0; i < seg.intersections.size(); ++i) {
                        const SegmentIntersection& intrsctn = seg.intersections[i];
                        if (intrsctn.is_outer()) {
                            assert(intrsctn.is_low() || i > 0);
                            bool consumed = intrsctn.is_low() ?
                                intrsctn.consumed_vertical_up :
                                seg.intersections[i - 1].consumed_vertical_up;
                            if (!consumed) {
                                coordf_t dist2 = sqr(coordf_t(pointLast(0) - seg.pos)) + sqr(coordf_t(pointLast(1) - intrsctn.pos()));
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
            pointLast = Point(segs[i_vline].pos, segs[i_vline].intersections[i_intersection].pos());
            polyline_current->points.push_back(pointLast);
        }

        // From the initial point (i_vline, i_intersection), follow a path.
        SegmentedIntersectionLine& seg = segs[i_vline];
        SegmentIntersection* intrsctn = &seg.intersections[i_intersection];
        bool going_up = intrsctn->is_low();
        bool try_connect = false;
        if (going_up) {
            assert(!intrsctn->consumed_vertical_up);
            assert(i_intersection + 1 < seg.intersections.size());
            // Step back to the beginning of the vertical segment to mark it as consumed.
            if (intrsctn->is_inner()) {
                assert(i_intersection > 0);
                --intrsctn;
                --i_intersection;
            }
            // Consume the complete vertical segment up to the outer contour.
            do {
                intrsctn->consumed_vertical_up = true;
                ++intrsctn;
                ++i_intersection;
                assert(i_intersection < seg.intersections.size());
            } while (intrsctn->type != SegmentIntersection::OUTER_HIGH);
            if ((intrsctn - 1)->is_inner()) {
                // Step back.
                --intrsctn;
                --i_intersection;
                assert(intrsctn->type == SegmentIntersection::INNER_HIGH);
                try_connect = true;
            }
        } else {
            // Going down.
            assert(intrsctn->is_high());
            assert(i_intersection > 0);
            assert(!(intrsctn - 1)->consumed_vertical_up);
            // Consume the complete vertical segment up to the outer contour.
            if (intrsctn->is_inner())
                intrsctn->consumed_vertical_up = true;
            do {
                assert(i_intersection > 0);
                --intrsctn;
                --i_intersection;
                intrsctn->consumed_vertical_up = true;
            } while (intrsctn->type != SegmentIntersection::OUTER_LOW);
            if ((intrsctn + 1)->is_inner()) {
                // Step back.
                ++intrsctn;
                ++i_intersection;
                assert(intrsctn->type == SegmentIntersection::INNER_LOW);
                try_connect = true;
            }
        }
        if (try_connect) {
            // Decide, whether to finish the segment, or whether to follow the perimeter.

            // 1) Find possible connection points on the previous / next vertical line.
            IntersectionTypeOtherVLine intrsctn_type_prev = intersection_type_on_prev_vertical_line(segs, i_vline, i_intersection);
            IntersectionTypeOtherVLine intrsctn_type_next = intersection_type_on_next_vertical_line(segs, i_vline, i_intersection);
            // Try to connect to a previous or next vertical line, making a zig-zag pattern.
            if (intrsctn_type_prev == INTERSECTION_TYPE_OTHER_VLINE_OK || intrsctn_type_next == INTERSECTION_TYPE_OTHER_VLINE_OK) {
            	int iPrev = intrsctn->left_horizontal();
            	int iNext = intrsctn->right_horizontal();
                coordf_t distPrev = (intrsctn_type_prev != INTERSECTION_TYPE_OTHER_VLINE_OK) ? std::numeric_limits<coord_t>::max() :
                    measure_perimeter_prev_segment_length(poly_with_offset, segs, i_vline, i_intersection, iPrev);
                coordf_t distNext = (intrsctn_type_next != INTERSECTION_TYPE_OTHER_VLINE_OK) ? std::numeric_limits<coord_t>::max() :
                    measure_perimeter_next_segment_length(poly_with_offset, segs, i_vline, i_intersection, iNext);
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
                    polyline_current->points.push_back(Point(seg.pos, intrsctn->pos()));
                    polylines_out.push_back(Polyline());
                    polyline_current = &polylines_out.back();
                    const SegmentedIntersectionLine& il2 = segs[take_next ? (i_vline + 1) : (i_vline - 1)];
                    polyline_current->points.push_back(Point(il2.pos, il2.intersections[take_next ? iNext : iPrev].pos()));
                } else {
                    polyline_current->points.push_back(Point(seg.pos, intrsctn->pos()));
                    emit_perimeter_prev_next_segment(poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection, take_next ? iNext : iPrev, *polyline_current, take_next);
                }
                // Mark both the left and right connecting segment as consumed, because one cannot go to this intersection point as it has been consumed.
                if (iPrev != -1)
                    segs[i_vline - 1].intersections[iPrev].consumed_perimeter_right = true;
                if (iNext != -1)
                    intrsctn->consumed_perimeter_right = true;
                //FIXME consume the left / right connecting segments at the other end of this line? Currently it is not critical because a perimeter segment is not followed if the vertical segment at the other side has already been consumed.
                // Advance to the neighbor line.
                if (take_next) {
                    ++i_vline;
                    i_intersection = iNext;
                }
                else {
                    --i_vline;
                    i_intersection = iPrev;
                }
                continue;
            }

            // 5) Try to connect to a previous or next point on the same vertical line.
            if (int inext = intrsctn->vertical_outside(); inext != -1) {
                bool valid = true;
                // Verify, that there is no intersection with the inner contour up to the end of the contour segment.
                // Verify, that the successive segment has not been consumed yet.
                if (going_up) {
                    if (seg.intersections[inext].consumed_vertical_up)
                        valid = false;
                    else {
                        for (int i = (int)i_intersection + 1; i < inext && valid; ++i)
                            if (seg.intersections[i].is_inner())
                                valid = false;
                    }
                } else {
                    if (seg.intersections[inext - 1].consumed_vertical_up)
                        valid = false;
                    else {
                        for (int i = inext + 1; i < (int)i_intersection && valid; ++i)
                            if (seg.intersections[i].is_inner())
                                valid = false;
                    }
                }
                if (valid) {
                    const Polygon& poly = poly_with_offset.contour(intrsctn->iContour);
                    assert(intrsctn->iContour == seg.intersections[inext].iContour);
                    int iSegNext = seg.intersections[inext].iSegment;
                    // Skip this perimeter line?
                    bool skip = params.dont_connect;
                    bool dir_forward = intrsctn->has_right_vertical_outside();
                    if (! skip && link_max_length > 0) {
                        coordf_t link_length = measure_perimeter_segment_on_vertical_line_length(
                            poly_with_offset, segs, i_vline, i_intersection, inext, dir_forward);
                        skip = link_length > link_max_length;
                    }
                    polyline_current->points.push_back(Point(seg.pos, intrsctn->pos()));
                    if (skip) {
                        // Just skip the connecting contour and start a new path.
                        polylines_out.push_back(Polyline());
                        polyline_current = &polylines_out.back();
                        polyline_current->points.push_back(Point(seg.pos, seg.intersections[inext].pos()));
                    } else {
                        // Consume the connecting contour and the next segment.
                        emit_perimeter_segment_on_vertical_line(poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection, inext, *polyline_current, dir_forward);
                    }
                    // Mark both the left and right connecting segment as consumed, because one cannot go to this intersection point as it has been consumed.
                    // If there are any outer intersection points skipped (bypassed) by the contour,
                    // mark them as processed.
                    if (going_up) {
                        for (int i = (int)i_intersection; i < inext; ++i)
                            seg.intersections[i].consumed_vertical_up = true;
                    } else {
                        for (int i = inext; i < (int)i_intersection; ++i)
                            seg.intersections[i].consumed_vertical_up = true;
                    }
                    // seg.intersections[going_up ? i_intersection : i_intersection - 1].consumed_vertical_up = true;
                    intrsctn->consumed_perimeter_right = true;
                    i_intersection = inext;
                    if (going_up)
                        ++intrsctn;
                    else
                        --intrsctn;
                    intrsctn->consumed_perimeter_right = true;
                    continue;
                }
            }
        dont_connect:
            // No way to continue the current polyline. Take the rest of the line up to the outer contour.
            // This will finish the polyline, starting another polyline at a new point.
            if (going_up)
                ++intrsctn;
            else
                --intrsctn;
        }

        // Finish the current vertical line,
        // reset the current vertical line to pick a new starting point in the next round.
        assert(intrsctn->is_outer());
        assert(intrsctn->is_high() == going_up);
        pointLast = Point(seg.pos, intrsctn->pos());
        polyline_current->points.push_back(pointLast);
        // Handle duplicate points and zero length segments.
        polyline_current->remove_duplicate_points();
        assert(!polyline_current->has_duplicate_points());
        // Handle nearly zero length edges.
        if (polyline_current->points.size() <= 1 ||
            (polyline_current->points.size() == 2 &&
                std::abs(polyline_current->points.front()(0) - polyline_current->points.back()(0)) < SCALED_EPSILON &&
                std::abs(polyline_current->points.front()(1) - polyline_current->points.back()(1)) < SCALED_EPSILON))
            polylines_out.pop_back();
        intrsctn = NULL;
        i_intersection = -1;
        polyline_current = NULL;
    }
}

struct MonotonousRegion;

struct NextMonotonousRegion
{
	MonotonousRegion *region;
	struct Path {
		float length { 0 }; 		// Length of the link to the next region.
		float visibility { 0 }; 	// 1 / length. Which length, just to the next region, or including the path accross the region?
		float pheromone { 0 }; 		// <0, 1>
	};
	enum Index : int {
		LowLow,
		LowHigh,
		HighLow,
		HighHigh
	};
	Path paths[4];
};

struct MonotonousRegion
{
    struct Boundary {
        int vline;
        int low;
        int high;
    };

    Boundary 	left;
    Boundary 	right;

    // Length when starting at left.low
    double 		len1;
    // Length when starting at left.high
    double 		len2;
    // If true, then when starting at left.low, then ending at right.high and vice versa.
    // If false, then ending at the same side as starting.
    bool 		flips;

    int 		left_intersection_point(bool region_flipped) const { return region_flipped ? left.high : left.low; }
    int 		right_intersection_point(bool region_flipped) const { return (region_flipped == flips) ? right.low : right.high; }

    // Left regions are used to track whether all regions left to this one have already been printed.
    boost::container::small_vector<MonotonousRegion*, 4>	left_neighbors;
    // Right regions are held to pick a next region to be extruded using the "Ant colony" heuristics.
    boost::container::small_vector<NextMonotonousRegion, 4>	right_neighbors;
};

struct MonotonousRegionLink
{
    MonotonousRegion    *region;
    bool 				 flipped;
    // Distance of right side of this region to left side of the next region, if the "flipped" flag of this region and the next region 
    // is applied as defined.
    NextMonotonousRegion::Path *next;
    // Distance of right side of this region to left side of the next region, if the "flipped" flag of this region and the next region
    // is applied in reverse order as if the zig-zags were flipped.
    NextMonotonousRegion::Path *next_flipped;
};

static const SegmentIntersection& vertical_run_bottom(const SegmentedIntersectionLine &vline, const SegmentIntersection &start)
{
	assert(start.is_inner());
	const SegmentIntersection *it = &start;
	// Find the lowest SegmentIntersection::INNER_LOW starting with right.
	for (;;) {
		while (it->type != SegmentIntersection::INNER_LOW)
			-- it;
		int down = it->vertical_down();
		if (down == -1 || it->vertical_down_quality() != SegmentIntersection::LinkQuality::Valid)
			break;
		it = &vline.intersections[down];
	}
	return *it;
}
static SegmentIntersection& vertical_run_bottom(SegmentedIntersectionLine& vline, SegmentIntersection& start)
{
    return const_cast<SegmentIntersection&>(vertical_run_bottom(std::as_const(vline), std::as_const(start)));
}

static const SegmentIntersection& vertical_run_top(const SegmentedIntersectionLine &vline, const SegmentIntersection &start)
{
	assert(start.is_inner());
	const SegmentIntersection *it = &start;
	// Find the lowest SegmentIntersection::INNER_LOW starting with right.
	for (;;) {
		while (it->type != SegmentIntersection::INNER_HIGH)
			++ it;
		int up = it->vertical_up();
		if (up == -1 || it->vertical_up_quality() != SegmentIntersection::LinkQuality::Valid)
			break;
		it = &vline.intersections[up];
	}
	return *it;
}
static SegmentIntersection& vertical_run_top(SegmentedIntersectionLine& vline, SegmentIntersection& start)
{
    return const_cast<SegmentIntersection&>(vertical_run_top(std::as_const(vline), std::as_const(start)));
}

static SegmentIntersection* left_overlap_bottom(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_left)
{
	SegmentIntersection *left = nullptr;
	for (SegmentIntersection *it = &start; it <= &end; ++ it) {
		int i = it->left_horizontal();
		if (i != -1) {
			left = &vline_left.intersections[i];
			break;
		}
	}
	return left == nullptr ? nullptr : &vertical_run_bottom(vline_left, *left);
}

static SegmentIntersection* left_overlap_top(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_left)
{
	SegmentIntersection *left = nullptr;
	for (SegmentIntersection *it = &end; it >= &start; -- it) {
		int i = it->left_horizontal();
		if (i != -1) {
			left = &vline_left.intersections[i];
			break;
		}
	}
	return left == nullptr ? nullptr : &vertical_run_top(vline_left, *left);
}

static std::pair<SegmentIntersection*, SegmentIntersection*> left_overlap(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_left)
{
	std::pair<SegmentIntersection*, SegmentIntersection*> out(nullptr, nullptr);
	out.first = left_overlap_bottom(start, end, vline_left);
	if (out.first != nullptr)
		out.second = left_overlap_top(start, end, vline_left);
	return out;
}

static std::pair<SegmentIntersection*, SegmentIntersection*> left_overlap(std::pair<SegmentIntersection*, SegmentIntersection*> &start_end, SegmentedIntersectionLine &vline_left)
{
	assert((start_end.first == nullptr) == (start_end.second == nullptr));
	return start_end.first == nullptr ? start_end : left_overlap(*start_end.first, *start_end.second, vline_left);
}

static SegmentIntersection* right_overlap_bottom(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_right)
{
	SegmentIntersection *right = nullptr;
	for (SegmentIntersection *it = &start; it <= &end; ++ it) {
		int i = it->right_horizontal();
		if (i != -1) {
			right = &vline_right.intersections[i];
			break;
		}
	}
	return right == nullptr ? nullptr : &vertical_run_bottom(vline_right, *right);
}

static SegmentIntersection* right_overlap_top(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_right)
{
	SegmentIntersection *right = nullptr;
	for (SegmentIntersection *it = &end; it >= &start; -- it) {
		int i = it->right_horizontal();
		if (i != -1) {
			right = &vline_right.intersections[i];
			break;
		}
	}
	return right == nullptr ? nullptr : &vertical_run_top(vline_right, *right);
}

static std::pair<SegmentIntersection*, SegmentIntersection*> right_overlap(SegmentIntersection &start, SegmentIntersection &end, SegmentedIntersectionLine &vline_right)
{
	std::pair<SegmentIntersection*, SegmentIntersection*> out(nullptr, nullptr);
	out.first = right_overlap_bottom(start, end, vline_right);
	if (out.first != nullptr)
		out.second = right_overlap_top(start, end, vline_right);
	return out;
}

static std::pair<SegmentIntersection*, SegmentIntersection*> right_overlap(std::pair<SegmentIntersection*, SegmentIntersection*> &start_end, SegmentedIntersectionLine &vline_right)
{
	assert((start_end.first == nullptr) == (start_end.second == nullptr));
	return start_end.first == nullptr ? start_end : right_overlap(*start_end.first, *start_end.second, vline_right);
}

static std::vector<MonotonousRegion> generate_montonous_regions(std::vector<SegmentedIntersectionLine> &segs)
{
	std::vector<MonotonousRegion> monotonous_regions;

    for (size_t i_vline_seed = 0; i_vline_seed < segs.size(); ++ i_vline_seed) {
        SegmentedIntersectionLine  &vline_seed = segs[i_vline_seed];
    	for (size_t i_intersection_seed = 1; i_intersection_seed + 1 < vline_seed.intersections.size(); ) {
	        while (i_intersection_seed + 1 < vline_seed.intersections.size() &&
	        	   vline_seed.intersections[i_intersection_seed].type != SegmentIntersection::INNER_LOW)
	        	++ i_intersection_seed;
			SegmentIntersection *start = &vline_seed.intersections[i_intersection_seed];
            SegmentIntersection *end   = &end_of_vertical_run_raw(*start);
			if (! start->consumed_vertical_up) {
				// Draw a new monotonous region starting with this segment.
				// while there is only a single right neighbor
				start->consumed_vertical_up = true;
		        size_t i_vline = i_vline_seed;
                std::pair<SegmentIntersection*, SegmentIntersection*> left(start, end);
				MonotonousRegion region;
				region.left.vline = i_vline;
				region.left.low   = left.first  - vline_seed.intersections.data();
				region.left.high  = left.second - vline_seed.intersections.data();
				region.right      = region.left;
				while (++ i_vline < segs.size()) {
			        SegmentedIntersectionLine  &vline_left	= segs[i_vline - 1];
			        SegmentedIntersectionLine  &vline_right = segs[i_vline];
					std::pair<SegmentIntersection*, SegmentIntersection*> right 	  = right_overlap(left, vline_right);
					std::pair<SegmentIntersection*, SegmentIntersection*> right_left  = left_overlap(right, vline_left);
					if (left != right_left)
						// Left & right draws don't overlap exclusively.
						break;
					region.right.vline = i_vline;
					region.right.low   = right.first  - vline_right.intersections.data();
					region.right.high  = right.second - vline_right.intersections.data();
					right.first->consumed_vertical_up = true;
					left = right;
				}
			}
			i_intersection_seed = end - vline_seed.intersections.data() + 1;
		}
    }

    return monotonous_regions;
}

static void connect_monotonous_regions(std::vector<MonotonousRegion> &regions, std::vector<SegmentedIntersectionLine> &segs)
{
	// Map from low intersection to left / right side of a monotonous region.
	using MapType = std::pair<SegmentIntersection*, MonotonousRegion*>;
	std::vector<MapType> map_intersection_to_region_start;
	std::vector<MapType> map_intersection_to_region_end;
	map_intersection_to_region_start.reserve(regions.size());
	map_intersection_to_region_end.reserve(regions.size());
	for (MonotonousRegion &region : regions) {
		map_intersection_to_region_start.emplace_back(&segs[region.left.vline].intersections[region.left.low], &region);
		map_intersection_to_region_end.emplace_back(&segs[region.right.vline].intersections[region.right.low], &region);
	}
	auto intersections_lower = [](const MapType &l, const MapType &r){ return l.first < r.first ; };
	auto intersections_equal = [](const MapType &l, const MapType &r){ return l.first == r.first ; };
	std::sort(map_intersection_to_region_start.begin(), map_intersection_to_region_start.end(), intersections_lower);
	std::sort(map_intersection_to_region_end.begin(), map_intersection_to_region_end.end(), intersections_lower);

	// Scatter links to neighboring regions.
	for (MonotonousRegion &region : regions) {
		if (region.left.vline > 0) {
			auto &vline = segs[region.left.vline];
			auto  begin = &vline.intersections[region.left.low];
			auto  end   = &vline.intersections[region.left.high];
            for (;;) {
                MapType key(begin, nullptr);
                auto it = std::lower_bound(map_intersection_to_region_end.begin(), map_intersection_to_region_end.end(), key);
                assert(it != map_intersection_to_region_end.end() && it->first == key.first);
                NextMonotonousRegion next_region{ &region };
				it->second->right_neighbors.emplace_back(next_region);
				SegmentIntersection *next = &vertical_run_top(vline, *begin);
				if (next == end)
					break;
				while (next->type != SegmentIntersection::INNER_LOW)
					++ next;
				begin = next;
			}
		}
		if (region.right.vline + 1 < segs.size()) {
			auto &vline = segs[region.right.vline];
			auto  begin = &vline.intersections[region.right.low];
			auto  end   = &vline.intersections[region.right.high];
			for (;;) {
				MapType key(begin, nullptr);
				auto it = std::lower_bound(map_intersection_to_region_start.begin(), map_intersection_to_region_start.end(), key);
				assert(it != map_intersection_to_region_start.end() && it->first == key.first);
				it->second->left_neighbors.emplace_back(&region);
				SegmentIntersection *next = &vertical_run_top(vline, *begin);
				if (next == end)
					break;
				while (next->type != SegmentIntersection::INNER_LOW)
					++ next;
				begin = next;
			}
		}
	}
}

// Raad Salman: Algorithms for the Precedence Constrained Generalized Travelling Salesperson Problem
// https://www.chalmers.se/en/departments/math/research/research-groups/optimization/OptimizationMasterTheses/MScThesis-RaadSalman-final.pdf
// Algorithm 6.1 Lexicographic Path Preserving 3-opt
// Optimize path while maintaining the ordering constraints.
void monotonous_3_opt(std::vector<MonotonousRegionLink> &path, std::vector<SegmentedIntersectionLine> &segs)
{
	// When doing the 3-opt path preserving flips, one has to fulfill two constraints:
	//
	// 1) The new path should be shorter than the old path.
	// 2) The precedence constraints shall be satisified on the new path.
	//
	// Branch & bound with KD-tree may be used with the shorter path constraint, but the precedence constraint will have to be recalculated for each
	// shorter path candidate found, which has a quadratic cost for a dense precedence graph. For a sparse precedence graph the precedence
	// constraint verification will be cheaper.
	//
	// On the other side, if the full search space is traversed as in the diploma thesis by Raad Salman (page 24, Algorithm 6.1 Lexicographic Path Preserving 3-opt),
	// then the precedence constraint verification is amortized inside the O(n^3) loop. Now which is better for our task?
	//
	// It is beneficial to also try flipping of the infill zig-zags, for which a prefix sum of both flipped and non-flipped paths over
	// MonotonousRegionLinks may be utilized, however updating the prefix sum has a linear complexity, the same complexity as doing the 3-opt
	// exchange by copying the pieces.
}

// Find a run through monotonous infill blocks using an 'Ant colony" optimization method.
static std::vector<MonotonousRegionLink> chain_monotonous_regions(
	std::vector<MonotonousRegion> &regions, std::vector<SegmentedIntersectionLine> &segs, std::mt19937_64 &rng)
{
	// Start point of a region (left) given the direction of the initial infill line.
	auto region_start_point = [&segs](const MonotonousRegion &region, bool dir) {
		SegmentedIntersectionLine 	&vline  = segs[region.left.vline];
		SegmentIntersection      	&ipt    = vline.intersections[dir ? region.left.high : region.left.low];
		return Vec2f(float(vline.pos), float(ipt.pos()));
	};
	// End point of a region (right) given the direction of the initial infill line and whether the monotonous run contains
	// even or odd number of vertical lines.
    auto region_end_point = [&segs](const MonotonousRegion &region, bool dir) {
		SegmentedIntersectionLine 	&vline  = segs[region.right.vline];
		SegmentIntersection      	&ipt    = vline.intersections[(dir == region.flips) ? region.right.low : region.right.high];
		return Vec2f(float(vline.pos), float(ipt.pos()));
	};

	// Number of left neighbors (regions that this region depends on, this region cannot be printed before the regions left of it are printed).
	std::vector<int32_t>			left_neighbors_unprocessed(regions.size(), 0);
	// Queue of regions, which have their left neighbors already printed.
	std::vector<MonotonousRegion*> 	queue;
	queue.reserve(regions.size());
	for (MonotonousRegion &region : regions)
		if (region.left_neighbors.empty())
			queue.emplace_back(&region);
		else
			left_neighbors_unprocessed[&region - regions.data()] = region.left_neighbors.size();
	// Make copy of structures that need to be initialized at each ant iteration.
	auto left_neighbors_unprocessed_initial = left_neighbors_unprocessed;
	auto queue_initial 						= queue;

	std::vector<MonotonousRegionLink> path, best_path;
	path.reserve(regions.size());
	best_path.reserve(regions.size());
	float best_path_length = std::numeric_limits<float>::max();

	struct NextCandidate {
        NextMonotonousRegion        *region;
        NextMonotonousRegion::Path  *link;
        NextMonotonousRegion::Path  *link_flipped;
        float                        cost;
		bool 			             dir;
	};
	std::vector<NextCandidate> next_candidates;

	// How many times to repeat the ant simulation.
	constexpr int num_runs = 10;
	// With how many ants each of the run will be performed?
	constexpr int num_ants = 10;
	// Base (initial) pheromone level.
	constexpr float pheromone_initial_deposit = 0.5f;
	// Evaporation rate of pheromones.
	constexpr float pheromone_evaporation = 0.1f;
	// Probability at which to take the next best path. Otherwise take the the path based on the cost distribution.
	constexpr float probability_take_best = 0.9f;
	// Exponents of the cost function.
	constexpr float pheromone_alpha = 1.f; // pheromone exponent
	constexpr float pheromone_beta  = 2.f; // attractiveness weighted towards edge length
	// Cost of traversing a link between two monotonous regions.
	auto path_cost = [pheromone_alpha, pheromone_beta](NextMonotonousRegion::Path &path) {
		return pow(path.pheromone, pheromone_alpha) * pow(path.visibility, pheromone_beta);
	};
	for (int run = 0; run < num_runs; ++ run)
	{
		for (int ant = 0; ant < num_ants; ++ ant) 
		{
			// Find a new path following the pheromones deposited by the previous ants.
			path.clear();
			queue = queue_initial;
			left_neighbors_unprocessed = left_neighbors_unprocessed_initial;
			while (! queue.empty()) {
				// Sort the queue by distance to the last point.
				// Take a candidate based on shortest distance? or ant colony?
				if (path.empty()) {
					// Pick randomly the first from the queue at random orientation.
					int first_idx = std::uniform_int_distribution<>(0, int(queue.size()) - 1)(rng);
                    path.emplace_back(MonotonousRegionLink{ queue[first_idx], rng() > rng.max() / 2 });
					*(queue.begin() + first_idx) = std::move(queue.back());
					queue.pop_back();
				} else {
					// Pick the closest neighbor from the queue?
				}
				-- left_neighbors_unprocessed[path.back().region - regions.data()];
				while (! path.back().region->right_neighbors.empty()) {
					// Chain.
					MonotonousRegion 		    &region = *path.back().region;
					bool 			  			 dir    = path.back().flipped;
					Vec2f                    	 end_pt	= region_end_point(region, dir);
					// Sort by distance to pt.
					next_candidates.reserve(region.right_neighbors.size() * 2);
					for (NextMonotonousRegion &next : region.right_neighbors) {
						int unprocessed = left_neighbors_unprocessed[next.region - regions.data()];
						assert(unprocessed > 0);
						if (unprocessed == 1) {
							// Dependencies of the successive blocks are satisfied.
                            bool flip = dir == region.flips;
                            auto path_cost = [pheromone_alpha, pheromone_beta](NextMonotonousRegion::Path& path) {
                                return pow(path.pheromone, pheromone_alpha) * pow(path.visibility, pheromone_beta);
                            };
                            NextMonotonousRegion::Path &path_low  		  = next.paths[flip ? NextMonotonousRegion::HighLow  : NextMonotonousRegion::LowLow];
                            NextMonotonousRegion::Path &path_low_flipped  = next.paths[flip ? NextMonotonousRegion::LowHigh  : NextMonotonousRegion::HighHigh];
                            NextMonotonousRegion::Path &path_high 	      = next.paths[flip ? NextMonotonousRegion::HighHigh : NextMonotonousRegion::LowHigh];
                            NextMonotonousRegion::Path &path_high_flipped = next.paths[flip ? NextMonotonousRegion::LowLow   : NextMonotonousRegion::HighLow];
                            next_candidates.emplace_back(NextCandidate{ &next, &path_low,  &path_low_flipped,  path_cost(path_low),  false });
                            next_candidates.emplace_back(NextCandidate{ &next, &path_high, &path_high_flipped, path_cost(path_high), true });
						}
					}
					//std::sort(next_candidates.begin(), next_candidates.end(), [](const auto &l, const auto &r) { l.dist < r.dist; });
					float dice = float(rng()) / float(rng.max());
                    std::vector<NextCandidate>::iterator take_path;
					if (dice < probability_take_best) {
						// Take the lowest cost path.
						take_path = std::min_element(next_candidates.begin(), next_candidates.end(), [](auto &l, auto &r){ return l.cost < r.cost; });
					} else {
						// Take the path based on the cost.
                        // Calculate the total cost.
                        float total_cost = std::accumulate(next_candidates.begin(), next_candidates.end(), 0.f, [](const float l, const NextCandidate& r) { return l + r.cost; });
						// Take a random path based on the cost.
                        float cost_threshold = floor(float(rng()) * total_cost / float(rng.max()));
                        take_path = next_candidates.end();
                        -- take_path;
                        for (auto it = next_candidates.begin(); it < next_candidates.end(); ++ it)
                            if (cost_threshold -= it->cost <= 0.) {
                                take_path = it;
                                break;
                            }
					}
					// Extend the path.
					NextMonotonousRegion &next_region = *take_path->region;
					bool        		  next_dir    = take_path->dir;
                    path.back().next         = take_path->link;
                    path.back().next_flipped = take_path->link_flipped;
                    path.emplace_back(MonotonousRegionLink{ next_region.region, next_dir });
					// Decrease the number of next block dependencies.
					-- left_neighbors_unprocessed[next_region.region - regions.data()];
					// Update pheromones along this link.
					take_path->link->pheromone = (1.f - pheromone_evaporation) * take_path->link->pheromone + pheromone_evaporation * pheromone_initial_deposit;
				}
			}

			// Perform 3-opt local optimization of the path.
			monotonous_3_opt(path, segs);

			// Measure path length.
            float path_length = std::accumulate(path.begin(), path.end(), 0.f, [](const float l, const MonotonousRegionLink& r) { return l + r.next->length; });
			// Save the shortest path.
			if (path_length < best_path_length) {
				best_path_length = path_length;
				std::swap(best_path_length, path_length);
			}
		}

		// Reinforce the path feromones with the best path.
        float total_cost = best_path_length;
		for (MonotonousRegionLink &link : path)
            link.next->pheromone = (1.f - pheromone_evaporation) * link.next->pheromone + pheromone_evaporation / total_cost;
	}

	return best_path;
}

// Traverse path, produce polylines.
static void polylines_from_paths(const std::vector<MonotonousRegionLink> &path, const ExPolygonWithOffset &poly_with_offset, const std::vector<SegmentedIntersectionLine> &segs, Polylines &polylines_out)
{
	Polyline *polyline = nullptr;
	auto finish_polyline = [&polyline, &polylines_out]() {
        polyline->remove_duplicate_points();
        // Handle duplicate points and zero length segments.
        assert(!polyline->has_duplicate_points());
        // Handle nearly zero length edges.
        if (polyline->points.size() <= 1 ||
            (polyline->points.size() == 2 &&
                std::abs(polyline->points.front()(0) - polyline->points.back()(0)) < SCALED_EPSILON &&
                std::abs(polyline->points.front()(1) - polyline->points.back()(1)) < SCALED_EPSILON))
            polylines_out.pop_back();
    	polyline = nullptr;
    };

	for (const MonotonousRegionLink &path_segment : path) {
		MonotonousRegion &region = *path_segment.region;
		bool 			  dir    = path_segment.flipped;

        // From the initial point (i_vline, i_intersection), follow a path.
		int  i_intersection = region.left_intersection_point(dir);
		int  i_vline 		= region.left.vline;

        if (polyline != nullptr && &path_segment != path.data()) {
        	// Connect previous path segment with the new one.
        	const MonotonousRegionLink 	      &path_segment_prev  = *(&path_segment - 1);
			const MonotonousRegion 		      &region_prev		  = *path_segment_prev.region;
			bool 			  			       dir_prev 		  = path_segment_prev.flipped;
			int                                i_vline_prev       = region_prev.right.vline;
			const SegmentedIntersectionLine   &vline_prev         = segs[i_vline_prev];
			int 		       				   i_intersection_prev = region_prev.right_intersection_point(dir_prev);
			const SegmentIntersection         *ip_prev 			  = &vline_prev.intersections[i_intersection_prev];
			bool 						       extended           = false;
			if (i_vline_prev + 1 == i_vline) {
				if (ip_prev->right_horizontal() == i_intersection && ip_prev->next_on_contour_quality == SegmentIntersection::LinkQuality::Valid) {
		        	// Emit a horizontal connection contour.
		            emit_perimeter_prev_next_segment(poly_with_offset, segs, i_vline_prev, ip_prev->iContour, i_intersection_prev, i_intersection, *polyline, true);
		            extended = true;
				}
	        }
	        if (! extended) {
		        // Finish the current vertical line,
                assert(ip_prev->is_inner());
                ip_prev->is_low() ? -- ip_prev : ++ ip_prev;
		        assert(ip_prev->is_outer());
	        	polyline->points.back() = Point(vline_prev.pos, ip_prev->pos());
				finish_polyline();
			}
        }

		for (;;) {
	        const SegmentedIntersectionLine &seg = segs[i_vline];
            const SegmentIntersection       *intrsctn = &seg.intersections[i_intersection];
            const bool                       going_up = intrsctn->is_low();
            if (polyline == nullptr) {
				polylines_out.emplace_back();
	            polyline = &polylines_out.back();
	            // Extend the infill line up to the outer contour.
	        	polyline->points.emplace_back(seg.pos, (intrsctn + (going_up ? - 1 : 1))->pos());
			} else
				polyline->points.emplace_back(seg.pos, intrsctn->pos());

			int iright = intrsctn->right_horizontal();
	        if (going_up) {
	            // Consume the complete vertical segment up to the inner contour.
	            for (;;) {
		            do {
		                ++ intrsctn;
						iright = std::max(iright, intrsctn->right_horizontal());
		            } while (intrsctn->type != SegmentIntersection::INNER_HIGH);
	                polyline->points.emplace_back(seg.pos, intrsctn->pos());
		            int inext = intrsctn->vertical_up();
		            if (inext == -1)
		            	break;
		            const Polygon &poly = poly_with_offset.contour(intrsctn->iContour);
	                assert(intrsctn->iContour == seg.intersections[inext].iContour);
	                emit_perimeter_segment_on_vertical_line(poly_with_offset, segs, i_vline, intrsctn->iContour, i_intersection, inext, *polyline, intrsctn->has_right_vertical_up());
	                intrsctn = seg.intersections.data() + inext;
	            } 
	        } else {
	            // Going down.
	            assert(intrsctn->is_high());
	            assert(i_intersection > 0);
	            for (;;) {
		            do {
		                -- intrsctn;
		                if (int iright_new = intrsctn->right_horizontal(); iright_new != -1)
		                	iright = iright_new;
		            } while (intrsctn->type != SegmentIntersection::INNER_LOW);
	                polyline->points.emplace_back(seg.pos, intrsctn->pos());
		            int inext = intrsctn->vertical_down();
		            if (inext == -1)
		            	break;
		            const Polygon &poly = poly_with_offset.contour(intrsctn->iContour);
	                assert(intrsctn->iContour == seg.intersections[inext].iContour);
	                emit_perimeter_segment_on_vertical_line(poly_with_offset, segs, i_vline, intrsctn->iContour, intrsctn - seg.intersections.data(), inext, *polyline, intrsctn->has_right_vertical_down());
	                intrsctn = seg.intersections.data() + inext;
	            } 
	        }

	        if (i_vline == region.right.vline)
	        	break;

	        int inext = intrsctn->right_horizontal();
	        if (inext != -1 && intrsctn->next_on_contour_quality == SegmentIntersection::LinkQuality::Valid) {
	        	// Emit a horizontal connection contour.
	            emit_perimeter_prev_next_segment(poly_with_offset, segs, i_vline, intrsctn->iContour, intrsctn - seg.intersections.data(), inext, *polyline, true);
	            i_intersection = inext;
	        } else {
		        // Finish the current vertical line,
	        	going_up ? ++ intrsctn : -- intrsctn;
		        assert(intrsctn->is_outer());
		        assert(intrsctn->is_high() == going_up);
	        	polyline->points.back() = Point(seg.pos, intrsctn->pos());
				finish_polyline();
				if (inext == -1) {
					// Find the end of the next overlapping vertical segment.
			        const SegmentedIntersectionLine &vline_right = segs[i_vline + 1];
                    const SegmentIntersection       *right       = going_up ? 
                        &vertical_run_top(vline_right, vline_right.intersections[iright]) : &vertical_run_bottom(vline_right, vline_right.intersections[iright]);
					i_intersection = right - vline_right.intersections.data();
				} else
		            i_intersection = inext;
	        }

	        ++ i_vline;
	    }
    }
}

bool FillRectilinear2::fill_surface_by_lines(const Surface *surface, const FillParams &params, float angleBase, float pattern_shift, Polylines &polylines_out)
{
    // At the end, only the new polylines will be rotated back.
    size_t n_polylines_out_initial = polylines_out.size();

    // Shrink the input polygon a bit first to not push the infill lines out of the perimeters.
//    const float INFILL_OVERLAP_OVER_SPACING = 0.3f;
    const float INFILL_OVERLAP_OVER_SPACING = 0.45f;
    assert(INFILL_OVERLAP_OVER_SPACING > 0 && INFILL_OVERLAP_OVER_SPACING < 0.5f);

    // Rotate polygons so that we can work with vertical lines here
    std::pair<float, Point> rotate_vector = this->_infill_direction(surface);
    rotate_vector.first += angleBase;

    assert(params.density > 0.0001f && params.density <= 1.f);
    coord_t line_spacing = coord_t(scale_(this->spacing) / params.density);

    // On the polygons of poly_with_offset, the infill lines will be connected.
    ExPolygonWithOffset poly_with_offset(
        surface->expolygon, 
        - rotate_vector.first, 
        scale_(this->overlap - (0.5 - INFILL_OVERLAP_OVER_SPACING) * this->spacing),
        scale_(this->overlap - 0.5 * this->spacing));
    if (poly_with_offset.n_contours_inner == 0) {
        // Not a single infill line fits.
        //FIXME maybe one shall trigger the gap fill here?
        return true;
    }

    BoundingBox bounding_box = poly_with_offset.bounding_box_src();

    // define flow spacing according to requested density
    if (params.full_infill() && !params.dont_adjust) {
        line_spacing = this->_adjust_solid_spacing(bounding_box.size()(0), line_spacing);
        this->spacing = unscale<double>(line_spacing);
    } else {
        // extend bounding box so that our pattern will be aligned with other layers
        // Transform the reference point to the rotated coordinate system.
        Point refpt = rotate_vector.second.rotated(- rotate_vector.first);
        // _align_to_grid will not work correctly with positive pattern_shift.
        coord_t pattern_shift_scaled = coord_t(scale_(pattern_shift)) % line_spacing;
        refpt(0) -= (pattern_shift_scaled >= 0) ? pattern_shift_scaled : (line_spacing + pattern_shift_scaled);
        bounding_box.merge(_align_to_grid(
            bounding_box.min, 
            Point(line_spacing, line_spacing), 
            refpt));
    }

    // Intersect a set of euqally spaced vertical lines wiht expolygon.
    // n_vlines = ceil(bbox_width / line_spacing)
    size_t  n_vlines = (bounding_box.max(0) - bounding_box.min(0) + line_spacing - 1) / line_spacing;
	coord_t x0 = bounding_box.min(0);
	if (params.full_infill())
		x0 += (line_spacing + SCALED_EPSILON) / 2;

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

    std::vector<SegmentedIntersectionLine> segs = slice_region_by_vertical_lines(poly_with_offset, n_vlines, x0, line_spacing);
	connect_segment_intersections_by_contours(poly_with_offset, segs);

#ifdef SLIC3R_DEBUG
    // Paint the segments and finalize the SVG file.
    for (size_t i_seg = 0; i_seg < segs.size(); ++ i_seg) {
        SegmentedIntersectionLine &sil = segs[i_seg];
        for (size_t i = 0; i < sil.intersections.size();) {
            size_t j = i + 1;
            for (; j < sil.intersections.size() && sil.intersections[j].is_inner(); ++ j) ;
            if (i + 1 == j) {
                svg.draw(Line(Point(sil.pos, sil.intersections[i].pos()), Point(sil.pos, sil.intersections[j].pos())), "blue");
            } else {
                svg.draw(Line(Point(sil.pos, sil.intersections[i].pos()), Point(sil.pos, sil.intersections[i+1].pos())), "green");
                svg.draw(Line(Point(sil.pos, sil.intersections[i+1].pos()), Point(sil.pos, sil.intersections[j-1].pos())), (j - i + 1 > 4) ? "yellow" : "magenta");
                svg.draw(Line(Point(sil.pos, sil.intersections[j-1].pos()), Point(sil.pos, sil.intersections[j].pos())), "green");
            }
            i = j + 1;
        }
    }
    svg.Close();
#endif /* SLIC3R_DEBUG */

    bool monotonous_infill = params.density > 0.99;
    if (monotonous_infill) {
		std::vector<MonotonousRegion> regions = generate_montonous_regions(segs);
		connect_monotonous_regions(regions, segs);
		std::mt19937_64 rng;
		std::vector<MonotonousRegionLink> path = chain_monotonous_regions(regions, segs, rng);
		polylines_from_paths(path, poly_with_offset, segs, polylines_out);
	} else
		traverse_graph_generate_polylines(poly_with_offset, params, this->link_max_length, segs, polylines_out);

#ifdef SLIC3R_DEBUG
    {
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
        // it->translate(- rotate_vector.second(0), - rotate_vector.second(1));
        assert(! it->has_duplicate_points());
        it->rotate(rotate_vector.first);
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

Polylines FillRectilinear2::fill_surface(const Surface *surface, const FillParams &params)
{
    Polylines polylines_out;
    if (! fill_surface_by_lines(surface, params, 0.f, 0.f, polylines_out)) {
        printf("FillRectilinear2::fill_surface() failed to fill a region.\n");
    }
    return polylines_out;
}

Polylines FillGrid2::fill_surface(const Surface *surface, const FillParams &params)
{
    // Each linear fill covers half of the target coverage.
    FillParams params2 = params;
    params2.density *= 0.5f;
    Polylines polylines_out;
    if (! fill_surface_by_lines(surface, params2, 0.f, 0.f, polylines_out) ||
        ! fill_surface_by_lines(surface, params2, float(M_PI / 2.), 0.f, polylines_out)) {
        printf("FillGrid2::fill_surface() failed to fill a region.\n");
    }
    return polylines_out;
}

Polylines FillTriangles::fill_surface(const Surface *surface, const FillParams &params)
{
    // Each linear fill covers 1/3 of the target coverage.
    FillParams params2 = params;
    params2.density *= 0.333333333f;
    FillParams params3 = params2;
    params3.dont_connect = true;
    Polylines polylines_out;
    if (! fill_surface_by_lines(surface, params2, 0.f, 0., polylines_out) ||
        ! fill_surface_by_lines(surface, params2, float(M_PI / 3.), 0., polylines_out) ||
        ! fill_surface_by_lines(surface, params3, float(2. * M_PI / 3.), 0., polylines_out)) {
        printf("FillTriangles::fill_surface() failed to fill a region.\n");
    }
    return polylines_out;
}

Polylines FillStars::fill_surface(const Surface *surface, const FillParams &params)
{
    // Each linear fill covers 1/3 of the target coverage.
    FillParams params2 = params;
    params2.density *= 0.333333333f;
    FillParams params3 = params2;
    params3.dont_connect = true;
    Polylines polylines_out;
    if (! fill_surface_by_lines(surface, params2, 0.f, 0., polylines_out) ||
        ! fill_surface_by_lines(surface, params2, float(M_PI / 3.), 0., polylines_out) ||
        ! fill_surface_by_lines(surface, params3, float(2. * M_PI / 3.), 0.5 * this->spacing / params2.density, polylines_out)) {
        printf("FillStars::fill_surface() failed to fill a region.\n");
    }
    return polylines_out;
}

Polylines FillCubic::fill_surface(const Surface *surface, const FillParams &params)
{
    // Each linear fill covers 1/3 of the target coverage.
    FillParams params2 = params;
    params2.density *= 0.333333333f;
    FillParams params3 = params2;
    params3.dont_connect = true;
    Polylines polylines_out;
    coordf_t dx = sqrt(0.5) * z;
    if (! fill_surface_by_lines(surface, params2, 0.f, dx, polylines_out) ||
        ! fill_surface_by_lines(surface, params2, float(M_PI / 3.), - dx, polylines_out) ||
        // Rotated by PI*2/3 + PI to achieve reverse sloping wall.
        ! fill_surface_by_lines(surface, params3, float(M_PI * 2. / 3.), dx, polylines_out)) {
        printf("FillCubic::fill_surface() failed to fill a region.\n");
    } 
    return polylines_out; 
}

} // namespace Slic3r
