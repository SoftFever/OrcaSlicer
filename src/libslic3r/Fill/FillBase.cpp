#include <stdio.h>
#include <numeric>

#include "../ClipperUtils.hpp"
#include "../EdgeGrid.hpp"
#include "../Geometry.hpp"
#include "../Point.hpp"
#include "../PrintConfig.hpp"
#include "../Surface.hpp"
#include "../libslic3r.h"

#include "FillBase.hpp"
#include "FillConcentric.hpp"
#include "FillHoneycomb.hpp"
#include "Fill3DHoneycomb.hpp"
#include "FillGyroid.hpp"
#include "FillPlanePath.hpp"
#include "FillLine.hpp"
#include "FillRectilinear.hpp"
#include "FillAdaptive.hpp"

namespace Slic3r {

Fill* Fill::new_from_type(const InfillPattern type)
{
    switch (type) {
    case ipConcentric:          return new FillConcentric();
    case ipHoneycomb:           return new FillHoneycomb();
    case ip3DHoneycomb:         return new Fill3DHoneycomb();
    case ipGyroid:              return new FillGyroid();
    case ipRectilinear:         return new FillRectilinear();
    case ipMonotonic:           return new FillMonotonic();
    case ipLine:                return new FillLine();
    case ipGrid:                return new FillGrid();
    case ipTriangles:           return new FillTriangles();
    case ipStars:               return new FillStars();
    case ipCubic:               return new FillCubic();
    case ipArchimedeanChords:   return new FillArchimedeanChords();
    case ipHilbertCurve:        return new FillHilbertCurve();
    case ipOctagramSpiral:      return new FillOctagramSpiral();
    case ipAdaptiveCubic:       return new FillAdaptive::Filler();
    case ipSupportCubic:        return new FillAdaptive::Filler();
    default: throw Slic3r::InvalidArgument("unknown type");
    }
}

Fill* Fill::new_from_type(const std::string &type)
{
    const t_config_enum_values &enum_keys_map = ConfigOptionEnum<InfillPattern>::get_enum_values();
    t_config_enum_values::const_iterator it = enum_keys_map.find(type);
    return (it == enum_keys_map.end()) ? nullptr : new_from_type(InfillPattern(it->second));
}

// Force initialization of the Fill::use_bridge_flow() internal static map in a thread safe fashion even on compilers
// not supporting thread safe non-static data member initializers.
static bool use_bridge_flow_initializer = Fill::use_bridge_flow(ipGrid);

bool Fill::use_bridge_flow(const InfillPattern type)
{
	static std::vector<unsigned char> cached;
	if (cached.empty()) {
		cached.assign(size_t(ipCount), 0);
		for (size_t i = 0; i < cached.size(); ++ i) {
			auto *fill = Fill::new_from_type((InfillPattern)i);
			cached[i] = fill->use_bridge_flow();
			delete fill;
		}
	}
	return cached[type] != 0;
}

Polylines Fill::fill_surface(const Surface *surface, const FillParams &params)
{
    // Perform offset.
    Slic3r::ExPolygons expp = offset_ex(surface->expolygon, float(scale_(this->overlap - 0.5 * this->spacing)));
    // Create the infills for each of the regions.
    Polylines polylines_out;
    for (size_t i = 0; i < expp.size(); ++ i)
        _fill_surface_single(
            params,
            surface->thickness_layers,
            _infill_direction(surface),
            std::move(expp[i]),
            polylines_out);
    return polylines_out;
}

// Calculate a new spacing to fill width with possibly integer number of lines,
// the first and last line being centered at the interval ends.
// This function possibly increases the spacing, never decreases, 
// and for a narrow width the increase in spacing may become severe,
// therefore the adjustment is limited to 20% increase.
coord_t Fill::_adjust_solid_spacing(const coord_t width, const coord_t distance)
{
    assert(width >= 0);
    assert(distance > 0);
    // floor(width / distance)
    coord_t number_of_intervals = (width - EPSILON) / distance;
    coord_t distance_new = (number_of_intervals == 0) ? 
        distance : 
        ((width - EPSILON) / number_of_intervals);
    const coordf_t factor = coordf_t(distance_new) / coordf_t(distance);
    assert(factor > 1. - 1e-5);
    // How much could the extrusion width be increased? By 20%.
    const coordf_t factor_max = 1.2;
    if (factor > factor_max)
        distance_new = coord_t(floor((coordf_t(distance) * factor_max + 0.5)));
    return distance_new;
}

// Returns orientation of the infill and the reference point of the infill pattern.
// For a normal print, the reference point is the center of a bounding box of the STL.
std::pair<float, Point> Fill::_infill_direction(const Surface *surface) const
{
    // set infill angle
    float out_angle = this->angle;

	if (out_angle == FLT_MAX) {
		//FIXME Vojtech: Add a warning?
        printf("Using undefined infill angle\n");
        out_angle = 0.f;
    }

    // Bounding box is the bounding box of a perl object Slic3r::Print::Object (c++ object Slic3r::PrintObject)
    // The bounding box is only undefined in unit tests.
    Point out_shift = empty(this->bounding_box) ? 
    	surface->expolygon.contour.bounding_box().center() : 
        this->bounding_box.center();

#if 0
    if (empty(this->bounding_box)) {
        printf("Fill::_infill_direction: empty bounding box!");
    } else {
        printf("Fill::_infill_direction: reference point %d, %d\n", out_shift.x, out_shift.y);
    }
#endif

    if (surface->bridge_angle >= 0) {
	    // use bridge angle
		//FIXME Vojtech: Add a debugf?
        // Slic3r::debugf "Filling bridge with angle %d\n", rad2deg($surface->bridge_angle);
#ifdef SLIC3R_DEBUG
        printf("Filling bridge with angle %f\n", surface->bridge_angle);
#endif /* SLIC3R_DEBUG */
        out_angle = surface->bridge_angle;
    } else if (this->layer_id != size_t(-1)) {
        // alternate fill direction
        out_angle += this->_layer_angle(this->layer_id / surface->thickness_layers);
    } else {
//    	printf("Layer_ID undefined!\n");
    }

    out_angle += float(M_PI/2.);
    return std::pair<float, Point>(out_angle, out_shift);
}

// A single T joint of an infill line to a closed contour or one of its holes.
struct ContourIntersectionPoint {
    // Contour and point on a contour where an infill line is connected to.
    size_t                      contour_idx;
    size_t                      point_idx;
    // Eucleidean parameter of point_idx along its contour.
    float                       param;
    // Other intersection points along the same contour. If there is only a single T-joint on a contour
    // with an intersection line, then the prev_on_contour and next_on_contour remain nulls.
    ContourIntersectionPoint*   prev_on_contour { nullptr };
    ContourIntersectionPoint*   next_on_contour { nullptr };
    // Length of the contour not yet allocated to some extrusion path going back (clockwise), or masked out by some overlapping infill line.
    float                       contour_not_taken_length_prev { std::numeric_limits<float>::max() };
    // Length of the contour not yet allocated to some extrusion path going forward (counter-clockwise), or masked out by some overlapping infill line.
    float                       contour_not_taken_length_next { std::numeric_limits<float>::max() };
    // End point is consumed if an infill line connected to this T-joint was already connected left or right along the contour,
    // or if the infill line was processed, but it was not possible to connect it left or right along the contour.
    bool                        consumed { false };
    // Whether the contour was trimmed by an overlapping infill line, or whether part of this contour was connected to some infill line.
    bool                        prev_trimmed { false };
    bool                        next_trimmed { false };

    void                        consume_prev() { this->contour_not_taken_length_prev = 0.; this->prev_trimmed = true; this->consumed = true; }
    void                        consume_next() { this->contour_not_taken_length_next = 0.; this->next_trimmed = true; this->consumed = true; }

    void                        trim_prev(const float new_len) { 
        if (new_len < this->contour_not_taken_length_prev) {
            this->contour_not_taken_length_prev = new_len;
            this->prev_trimmed = true;
        }
    }
    void                        trim_next(const float new_len) { 
        if (new_len < this->contour_not_taken_length_next) {
            this->contour_not_taken_length_next = new_len;
            this->next_trimmed = true;
        }
    }

    // The end point of an infill line connected to this T-joint was not processed yet and a piece of the contour could be extruded going backwards.
    bool                        could_take_prev() const throw() { return ! this->consumed && this->contour_not_taken_length_prev > SCALED_EPSILON; }
    // The end point of an infill line connected to this T-joint was not processed yet and a piece of the contour could be extruded going forward.
    bool                        could_take_next() const throw() { return ! this->consumed && this->contour_not_taken_length_next > SCALED_EPSILON; }

    // Could extrude a complete segment from this to this->prev_on_contour.
    bool                        could_connect_prev() const throw() 
        { return ! this->consumed && this->prev_on_contour && ! this->prev_on_contour->consumed && ! this->prev_trimmed && ! this->prev_on_contour->next_trimmed; }
    // Could extrude a complete segment from this to this->next_on_contour.
    bool                        could_connect_next() const throw() 
        { return ! this->consumed && this->next_on_contour && ! this->next_on_contour->consumed && ! this->next_trimmed && ! this->next_on_contour->prev_trimmed; }
};

// Distance from param1 to param2 when going counter-clockwise.
static inline float closed_contour_distance_ccw(float param1, float param2, float contour_length)
{
    assert(param1 >= 0.f && param1 <= contour_length);
    assert(param2 >= 0.f && param2 <= contour_length);
    float d = param2 - param1;
    if (d < 0.f)
        d += contour_length;
    return d;
}

// Distance from param1 to param2 when going clockwise.
static inline float closed_contour_distance_cw(float param1, float param2, float contour_length)
{
    return closed_contour_distance_ccw(param2, param1, contour_length);
}

// Length along the contour from cp1 to cp2 going counter-clockwise.
float path_length_along_contour_ccw(const ContourIntersectionPoint *cp1, const ContourIntersectionPoint *cp2, float contour_length)
{
    assert(cp1 != nullptr);
    assert(cp2 != nullptr);
    assert(cp1->contour_idx == cp2->contour_idx);
    assert(cp1 != cp2);
    return closed_contour_distance_ccw(cp1->param, cp2->param, contour_length);
}

// Lengths along the contour from cp1 to cp2 going CCW and going CW.
std::pair<float, float> path_lengths_along_contour(const ContourIntersectionPoint *cp1, const ContourIntersectionPoint *cp2, float contour_length)
{
    // Zero'th param is the length of the contour.
    float param_lo  = cp1->param;
    float param_hi  = cp2->param;
    assert(param_lo >= 0.f && param_lo <= contour_length);
    assert(param_hi >= 0.f && param_hi <= contour_length);
    bool  reversed  = false;
    if (param_lo > param_hi) {
        std::swap(param_lo, param_hi);
        reversed = true;
    }
    auto out = std::make_pair(param_hi - param_lo, param_lo + contour_length - param_hi);
    if (reversed)
        std::swap(out.first, out.second);
    return out;
}

// Add contour points from interval (idx_start, idx_end> to polyline.
static inline void take_cw_full(Polyline &pl, const Points& contour, size_t idx_start, size_t idx_end)
{
    assert(! pl.empty() && pl.points.back() == contour[idx_start]);
    size_t i = (idx_end == 0) ? contour.size() - 1 : idx_start - 1;
    while (i != idx_end) {
        pl.points.emplace_back(contour[i]);
        if (i == 0)
            i = contour.size();
        --i;
    }
    pl.points.emplace_back(contour[i]);
}

// Add contour points from interval (idx_start, idx_end> to polyline, limited by the Eucleidean length taken.
static inline float take_cw_limited(Polyline &pl, const Points &contour, const std::vector<float> &params, size_t idx_start, size_t idx_end, float length_to_take)
{
    // If appending to an infill line, then the start point of a perimeter line shall match the end point of an infill line.
    assert(pl.empty() || pl.points.back() == contour[idx_start]);
    assert(contour.size() + 1 == params.size());
    assert(length_to_take > SCALED_EPSILON);
    // Length of the contour.
    float  length = params.back();
    // Parameter (length from contour.front()) for the first point.
    float  p0     = params[idx_start];
    // Current (2nd) point of the contour.
    size_t i      = (idx_start == 0) ? contour.size() - 1 : idx_start - 1;
    // Previous point of the contour.
    size_t iprev  = idx_start;
    // Length of the contour curve taken for iprev.
    float  lprev  = 0.f;

    for (;;) {
        float l = closed_contour_distance_cw(p0, params[i], length);
        if (l >= length_to_take) {
            // Trim the last segment.
            double t = double(length_to_take - lprev) / (l - lprev);
            pl.points.emplace_back(lerp(contour[iprev], contour[i], t));
            return length_to_take;
        }
        // Continue with the other segments.
        pl.points.emplace_back(contour[i]);
        if (i == idx_end)
            return l;
        iprev = i;
        lprev = l;
        if (i == 0)
            i = contour.size();
        -- i;
    }
    assert(false);
    return 0;
}

// Add contour points from interval (idx_start, idx_end> to polyline.
static inline void take_ccw_full(Polyline &pl, const Points &contour, size_t idx_start, size_t idx_end)
{
    assert(! pl.empty() && pl.points.back() == contour[idx_start]);
    size_t i = idx_start;
    if (++ i == contour.size())
        i = 0;
    while (i != idx_end) {
        pl.points.emplace_back(contour[i]);
        if (++ i == contour.size())
            i = 0;
    }
    pl.points.emplace_back(contour[i]);
}

// Add contour points from interval (idx_start, idx_end> to polyline, limited by the Eucleidean length taken.
// Returns length of the contour taken.
static inline float take_ccw_limited(Polyline &pl, const Points &contour, const std::vector<float> &params, size_t idx_start, size_t idx_end, float length_to_take)
{
    // If appending to an infill line, then the start point of a perimeter line shall match the end point of an infill line.
    assert(pl.empty() || pl.points.back() == contour[idx_start]);
    assert(contour.size() + 1 == params.size());
    assert(length_to_take > SCALED_EPSILON);
    // Length of the contour.
    float  length = params.back();
    // Parameter (length from contour.front()) for the first point.
    float  p0     = params[idx_start];
    // Current (2nd) point of the contour.
    size_t i      = idx_start;
    if (++ i == contour.size())
        i = 0;
    // Previous point of the contour.
    size_t iprev  = idx_start;
    // Length of the contour curve taken at iprev.
    float  lprev  = 0.f;
    for (;;) {
        float l = closed_contour_distance_ccw(p0, params[i], length);
        if (l >= length_to_take) {
            // Trim the last segment.
            double t = double(length_to_take - lprev) / (l - lprev);
            pl.points.emplace_back(lerp(contour[iprev], contour[i], t));
            return length_to_take;
        }
        // Continue with the other segments.
        pl.points.emplace_back(contour[i]);
        if (i == idx_end)
            return l;
        iprev = i;
        lprev = l;
        if (++ i == contour.size())
            i = 0;
    }
    assert(false);
    return 0;
}

// Connect end of pl1 to the start of pl2 using the perimeter contour.
// If clockwise, then a clockwise segment from idx_start to idx_end is taken, otherwise a counter-clockwise segment is being taken.
static void take(Polyline &pl1, const Polyline &pl2, const Points &contour, size_t idx_start, size_t idx_end, bool clockwise)
{
#ifndef NDEBUG
	assert(idx_start != idx_end);
    assert(pl1.size() >= 2);
    assert(pl2.size() >= 2);
#endif /* NDEBUG */

	{
		// Reserve memory at pl1 for the connecting contour and pl2.
		int new_points = int(idx_end) - int(idx_start) - 1;
		if (new_points < 0)
			new_points += int(contour.size());
		pl1.points.reserve(pl1.points.size() + size_t(new_points) + pl2.points.size());
	}

	if (clockwise)
        take_cw_full(pl1, contour, idx_start, idx_end);
	else
        take_ccw_full(pl1, contour, idx_start, idx_end);

    pl1.points.insert(pl1.points.end(), pl2.points.begin() + 1, pl2.points.end());
}

static void take(Polyline &pl1, const Polyline &pl2, const Points &contour, ContourIntersectionPoint *cp_start, ContourIntersectionPoint *cp_end, bool clockwise)
{
    assert(cp_start != cp_end);
    take(pl1, pl2, contour, cp_start->point_idx, cp_end->point_idx, clockwise);

    // Mark the contour segments in between cp_start and cp_end as consumed.
    if (clockwise)
        std::swap(cp_start, cp_end);
    if (cp_start->next_on_contour != cp_end)
        for (auto *cp = cp_start->next_on_contour; cp->next_on_contour != cp_end; cp = cp->next_on_contour) {
            cp->consume_prev();
            cp->consume_next();
        }
    cp_start->consume_next();
    cp_end->consume_prev();
}

static void take_limited(
    Polyline &pl1, const Points &contour, const std::vector<float> &params, 
    ContourIntersectionPoint *cp_start, ContourIntersectionPoint *cp_end, bool clockwise, float take_max_length, float line_half_width)
{
#ifndef NDEBUG
    assert(cp_start != cp_end);
    assert(pl1.size() >= 2);
    assert(contour.size() + 1 == params.size());
#endif /* NDEBUG */

    if (! (clockwise ? cp_start->could_take_prev() : cp_start->could_take_next()))
        return;

    assert(pl1.points.front() == contour[cp_start->point_idx] || pl1.points.back() == contour[cp_start->point_idx]);
    bool        add_at_start = pl1.points.front() == contour[cp_start->point_idx];
    Points      pl_tmp;
    if (add_at_start) {
        pl_tmp = std::move(pl1.points);
        pl1.points.clear();
    }

    {
        // Reserve memory at pl1 for the perimeter segment.
        // Pessimizing - take the complete segment.
        int new_points = int(cp_end->point_idx) - int(cp_start->point_idx) - 1;
        if (new_points < 0)
            new_points += int(contour.size());
        pl1.points.reserve(pl1.points.size() + pl_tmp.size() + size_t(new_points));
    }

    float length = params.back();
    float length_to_go = take_max_length;
    cp_start->consumed = true;
    if (clockwise) {
        // Going clockwise from cp_start to cp_end.
        for (ContourIntersectionPoint *cp = cp_start; cp != cp_end; cp = cp->prev_on_contour) {
            // Length of the segment from cp to cp->prev_on_contour.
            float l = closed_contour_distance_cw(cp->param, cp->prev_on_contour->param, length);
            length_to_go = std::min(length_to_go, cp->contour_not_taken_length_prev);
            //if (cp->prev_on_contour->consumed)
                // Don't overlap with an already extruded infill line.
                length_to_go = std::max(0.f, std::min(length_to_go, l - line_half_width));
            cp->consume_prev();
            if (l >= length_to_go) {
                if (length_to_go > SCALED_EPSILON) {
                    cp->prev_on_contour->trim_next(l - length_to_go);
                    take_cw_limited(pl1, contour, params, cp->point_idx, cp->prev_on_contour->point_idx, length_to_go);
                }
                break;
            } else {
                cp->prev_on_contour->trim_next(0.f);
                take_cw_full(pl1, contour, cp->point_idx, cp->prev_on_contour->point_idx);
                length_to_go -= l;
            }
        }
    } else {
        for (ContourIntersectionPoint *cp = cp_start; cp != cp_end; cp = cp->next_on_contour) {
            float l = closed_contour_distance_ccw(cp->param, cp->next_on_contour->param, length);
            length_to_go = std::min(length_to_go, cp->contour_not_taken_length_next);
            //if (cp->next_on_contour->consumed)
                // Don't overlap with an already extruded infill line.
                length_to_go = std::max(0.f, std::min(length_to_go, l - line_half_width));
            cp->consume_next();
            if (l >= length_to_go) {
                if (length_to_go > SCALED_EPSILON) {
                    cp->next_on_contour->trim_prev(l - length_to_go);
                    take_ccw_limited(pl1, contour, params, cp->point_idx, cp->next_on_contour->point_idx, length_to_go);
                }
                break;
            } else {
                cp->next_on_contour->trim_prev(0.f);
                take_ccw_full(pl1, contour, cp->point_idx, cp->next_on_contour->point_idx);
                length_to_go -= l;
            }
        }
    }

    if (add_at_start) {
        pl1.reverse();
        append(pl1.points, pl_tmp);
    }
}

// Return an index of start of a segment and a point of the clipping point at distance from the end of polyline.
struct SegmentPoint {
	// Segment index, defining a line <idx_segment, idx_segment + 1).
	size_t idx_segment = std::numeric_limits<size_t>::max();
	// Parameter of point in <0, 1) along the line <idx_segment, idx_segment + 1)
	double t;
	Vec2d  point;

	bool valid() const { return idx_segment != std::numeric_limits<size_t>::max(); }
};

static inline SegmentPoint clip_start_segment_and_point(const Points &polyline, double distance)
{
	assert(polyline.size() >= 2);
	assert(distance > 0.);
	// Initialized to "invalid".
	SegmentPoint out;
	if (polyline.size() >= 2) {
	    Vec2d pt_prev = polyline.front().cast<double>();
        for (size_t i = 1; i < polyline.size(); ++ i) {
			Vec2d pt = polyline[i].cast<double>();
			Vec2d v = pt - pt_prev;
	        double l = v.norm();
	        if (l > distance) {
	        	out.idx_segment = i - 1;
	        	out.t 			= distance / l;
	        	out.point 		= pt_prev + out.t * v;
	            break;
	        }
	        distance -= l;
	        pt_prev = pt;
	    }
	}
	return out;
}

static inline SegmentPoint clip_end_segment_and_point(const Points &polyline, double distance)
{
	assert(polyline.size() >= 2);
	assert(distance > 0.);
	// Initialized to "invalid".
	SegmentPoint out;
	if (polyline.size() >= 2) {
	    Vec2d pt_next = polyline.back().cast<double>();
		for (int i = int(polyline.size()) - 2; i >= 0; -- i) {
			Vec2d pt = polyline[i].cast<double>();
			Vec2d v = pt - pt_next;
	        double l = v.norm();
	        if (l > distance) {
	        	out.idx_segment = i;
	        	out.t 			= distance / l;
	        	out.point 		= pt_next + out.t * v;
				// Store the parameter referenced to the starting point of a segment.
				out.t			= 1. - out.t;
	            break;
	        }
	        distance -= l;
	        pt_next = pt;
	    }
	}
	return out;
}

// Calculate intersection of a line with a thick segment.
// Returns Eucledian parameters of the line / thick segment overlap.
static inline bool line_rounded_thick_segment_collision(
    const Vec2d &line_a,    const Vec2d &line_b, 
    const Vec2d &segment_a, const Vec2d &segment_b, const double offset, 
    std::pair<double, double> &out_interval)
{
    const Vec2d  line_v0   = line_b - line_a;
    double       lv        = line_v0.squaredNorm();

    const Vec2d  segment_v = segment_b - segment_a;
    const double segment_l = segment_v.norm();
    const double offset2   = offset * offset;

    bool intersects = false;
    if (lv < SCALED_EPSILON * SCALED_EPSILON)
    {
        // Very short line vector. Just test whether the center point is inside the offset line.
        Vec2d lpt = 0.5 * (line_a + line_b);
        if (segment_l > SCALED_EPSILON) {
            struct Linef { Vec2d a, b; };
            intersects = line_alg::distance_to_squared(Linef{ segment_a, segment_b }, lpt) < offset2;
        } else
            intersects = (0.5 * (segment_a + segment_b) - lpt).squaredNorm() < offset2;
        if (intersects) {
            out_interval.first = 0.;
            out_interval.second = sqrt(lv);
        }
    }
    else
    {
        // Output interval.
        double tmin = std::numeric_limits<double>::max();
        double tmax = -tmin;
        auto extend_interval = [&tmin, &tmax](double atmin, double atmax) {
            tmin = std::min(tmin, atmin);
            tmax = std::max(tmax, atmax);
        };

        // Intersections with the inflated segment end points.
        auto ray_circle_intersection_interval_extend = [&extend_interval, &line_v0](const Vec2d &segment_pt, const double offset2, const Vec2d &line_pt, const Vec2d &line_vec) {
            std::pair<Vec2d, Vec2d> pts;
            Vec2d  p0 = line_pt - segment_pt;
            double c  = - line_pt.dot(p0);
            if (Geometry::ray_circle_intersections_r2_lv2_c(offset2, line_vec.x(), line_vec.y(), line_vec.squaredNorm(), c, pts)) {
                double tmin = (pts.first  - p0).dot(line_v0);
                double tmax = (pts.second - p0).dot(line_v0);
                if (tmin > tmax)
                    std::swap(tmin, tmax);
                tmin = std::max(tmin, 0.);
                tmax = std::min(tmax, 1.);
                if (tmin <= tmax)
                    extend_interval(tmin, tmax);
            }
        };

        // Intersections with the inflated segment.
        if (segment_l > SCALED_EPSILON) {
            ray_circle_intersection_interval_extend(segment_a, offset2, line_a, line_v0);
            ray_circle_intersection_interval_extend(segment_b, offset2, line_a, line_v0);
            // Clip the line segment transformed into a coordinate space of the segment,
            // where the segment spans (0, 0) to (segment_l, 0).
            const Vec2d dir_x = segment_v / segment_l;
            const Vec2d dir_y(- dir_x.y(), dir_x.x());
            const Vec2d line_p0(line_a - segment_a);
            std::pair<double, double> interval;
            if (Geometry::liang_barsky_line_clipping_interval(
                    Vec2d(line_p0.dot(dir_x), line_p0.dot(dir_y)),
                    Vec2d(line_v0.dot(dir_x), line_v0.dot(dir_y)), 
                    BoundingBoxf(Vec2d(0., - offset), Vec2d(segment_l, offset)), 
                    interval))
                extend_interval(interval.first, interval.second);
        } else
            ray_circle_intersection_interval_extend(0.5 * (segment_a + segment_b), offset, line_a, line_v0);

        intersects = tmin <= tmax;
        if (intersects) {
            lv = sqrt(lv);
            out_interval.first  = tmin * lv;
            out_interval.second = tmax * lv;
        }
    }

#if 0
    {
        BoundingBox bbox;
        bbox.merge(line_a.cast<coord_t>());
        bbox.merge(line_a.cast<coord_t>());
        bbox.merge(segment_a.cast<coord_t>());
        bbox.merge(segment_b.cast<coord_t>());
        static int iRun = 0;
        ::Slic3r::SVG svg(debug_out_path("%s-%03d.svg", "line-thick-segment-intersect", iRun ++), bbox);
        svg.draw(Line(line_a.cast<coord_t>(), line_b.cast<coord_t>()), "black");
        svg.draw(Line(segment_a.cast<coord_t>(), segment_b.cast<coord_t>()), "blue", offset * 2.);
        svg.draw(segment_a.cast<coord_t>(), "blue", offset);
        svg.draw(segment_b.cast<coord_t>(), "blue", offset);
        svg.draw(Line(segment_a.cast<coord_t>(), segment_b.cast<coord_t>()), "black");
        if (intersects)
            svg.draw(Line((line_a + (line_b - line_a).normalized() * out_interval.first).cast<coord_t>(),
                          (line_a + (line_b - line_a).normalized() * out_interval.second).cast<coord_t>()), "red");
    }
#endif

    return intersects;
}

static inline bool inside_interval(float low, float high, float p)
{
    return p >= low && p <= high;
}

static inline bool interval_inside_interval(float outer_low, float outer_high, float inner_low, float inner_high, float epsilon)
{
    outer_low -= epsilon;
    outer_high += epsilon;
    return inside_interval(outer_low, outer_high, inner_low) && inside_interval(outer_low, outer_high, inner_high);
}

static inline bool cyclic_interval_inside_interval(float outer_low, float outer_high, float inner_low, float inner_high, float length)
{
    if (outer_low > outer_high)
        outer_high += length;
    if (inner_low > inner_high)
        inner_high += length;
    else if (inner_high < outer_low) {
        inner_low += length;
        inner_high += length;
    }
    return interval_inside_interval(outer_low, outer_high, inner_low, inner_high, float(SCALED_EPSILON));
}

// #define INFILL_DEBUG_OUTPUT

#ifdef INFILL_DEBUG_OUTPUT
static void export_infill_to_svg(
    // Boundary contour, along which the perimeter extrusions will be drawn.
    const std::vector<Points>                              &boundary,
    // Parametrization of boundary with Euclidian length.
    const std::vector<std::vector<float>>                  &boundary_parameters,
    // Intersections (T-joints) of the infill lines with the boundary.
    std::vector<std::vector<ContourIntersectionPoint*>>    &boundary_intersections,
    // Infill lines, either completely inside the boundary, or touching the boundary.
    const Polylines                                        &infill,
    const coord_t                                           scaled_spacing,
    const std::string                                      &path,
    const Polylines                                        &overlap_lines = Polylines(),
    const Polylines                                        &polylines = Polylines(),
    const Points                                           &pts = Points())
{
    Polygons    polygons;
    std::transform(boundary.begin(), boundary.end(), std::back_inserter(polygons), [](auto &pts) { return Polygon(pts); });
    ExPolygons  expolygons = union_ex(polygons);
    BoundingBox bbox = get_extents(polygons);
    bbox.offset(scale_(3.));

    ::Slic3r::SVG svg(path, bbox);
    // Draw the filled infill polygons.
    svg.draw(expolygons);

    // Draw the pieces of boundary allowed to be used as anchors of infill lines, not yet consumed.
    const std::string color_boundary_trimmed     = "blue";
    const std::string color_boundary_not_trimmed = "yellow";
    const coordf_t    boundary_line_width        = scaled_spacing;
    svg.draw_outline(polygons, "red", boundary_line_width);
    for (const std::vector<ContourIntersectionPoint*> &intersections : boundary_intersections) {
        const size_t                 boundary_idx  = &intersections - boundary_intersections.data();
        const Points                &contour       = boundary[boundary_idx];
        const std::vector<float>    &contour_param = boundary_parameters[boundary_idx];
        for (const ContourIntersectionPoint *ip : intersections) {
            assert(ip->next_trimmed == ip->next_on_contour->prev_trimmed);
            assert(ip->prev_trimmed == ip->prev_on_contour->next_trimmed);
            {
                Polyline pl { contour[ip->point_idx] };
                if (ip->next_trimmed) {
                    if (ip->contour_not_taken_length_next > SCALED_EPSILON) {
                        take_ccw_limited(pl, contour, contour_param, ip->point_idx, ip->next_on_contour->point_idx, ip->contour_not_taken_length_next);
                        svg.draw(pl, color_boundary_trimmed, boundary_line_width);
                    }
                } else {
                    take_ccw_full(pl, contour, ip->point_idx, ip->next_on_contour->point_idx);
                    svg.draw(pl, color_boundary_not_trimmed, boundary_line_width);
                }
            }
            {
                Polyline pl { contour[ip->point_idx] };
                if (ip->prev_trimmed) {
                    if (ip->contour_not_taken_length_prev > SCALED_EPSILON) {
                        take_cw_limited(pl, contour, contour_param, ip->point_idx, ip->prev_on_contour->point_idx, ip->contour_not_taken_length_prev);
                        svg.draw(pl, color_boundary_trimmed, boundary_line_width);
                    }
                } else {
                    take_cw_full(pl, contour, ip->point_idx, ip->prev_on_contour->point_idx);
                    svg.draw(pl, color_boundary_not_trimmed, boundary_line_width);
                }
            }
        }
    }

    // Draw the full infill polygon boundary.
    svg.draw_outline(polygons, "green");

    // Draw the infill lines, first the full length with red color, then a slightly shortened length with black color.
    svg.draw(infill, "brown");
    static constexpr double trim_length = scale_(0.15);
    for (Polyline polyline : infill)
        if (! polyline.empty()) {
            Vec2d a = polyline.points.front().cast<double>();
            Vec2d d = polyline.points.back().cast<double>();
            if (polyline.size() == 2) {
                Vec2d v = d - a;
                double l = v.norm();
                if (l > 2. * trim_length) {
                    a += v * trim_length / l;
                    d -= v * trim_length / l;
                    polyline.points.front() = a.cast<coord_t>();
                    polyline.points.back() = d.cast<coord_t>();
                } else
                    polyline.points.clear();
            } else if (polyline.size() > 2) {
                Vec2d b = polyline.points[1].cast<double>();
                Vec2d c = polyline.points[polyline.points.size() - 2].cast<double>();
                Vec2d v = b - a;
                double l = v.norm();
                if (l > trim_length) {
                    a += v * trim_length / l;
                    polyline.points.front() = a.cast<coord_t>();
                } else
                    polyline.points.erase(polyline.points.begin());
                v = d - c;
                l = v.norm();
                if (l > trim_length)
                    polyline.points.back() = (d - v * trim_length / l).cast<coord_t>();
                else
                    polyline.points.pop_back();
            }
            svg.draw(polyline, "black");
        }

    svg.draw(overlap_lines, "red", scale_(0.05));
    svg.draw(polylines, "magenta", scale_(0.05));
    svg.draw(pts, "magenta");
}
#endif // INFILL_DEBUG_OUTPUT

#ifndef NDEBUG
bool validate_boundary_intersections(const std::vector<std::vector<ContourIntersectionPoint*>> &boundary_intersections)
{
    for (const std::vector<ContourIntersectionPoint*>& contour : boundary_intersections) {
        for (ContourIntersectionPoint* ip : contour) {
            assert(ip->next_trimmed == ip->next_on_contour->prev_trimmed);
            assert(ip->prev_trimmed == ip->prev_on_contour->next_trimmed);
        }
    }
    return true;
}
#endif // NDEBUG

// Mark the segments of split boundary as consumed if they are very close to some of the infill line.
void mark_boundary_segments_touching_infill(
    // Boundary contour, along which the perimeter extrusions will be drawn.
	const std::vector<Points>                              &boundary,
    // Parametrization of boundary with Euclidian length.
	const std::vector<std::vector<float>>                  &boundary_parameters,
    // Intersections (T-joints) of the infill lines with the boundary.
    std::vector<std::vector<ContourIntersectionPoint*>>    &boundary_intersections,
    // Bounding box around the boundary.
	const BoundingBox 		                               &boundary_bbox,
    // Infill lines, either completely inside the boundary, or touching the boundary.
	const Polylines 		                               &infill,
    // How much of the infill ends should be ignored when marking the boundary segments?
	const double			                                clip_distance,
    // Roughly width of the infill line.
	const double 				                            distance_colliding)
{
    assert(boundary.size() == boundary_parameters.size());
#ifndef NDEBUG
    for (size_t i = 0; i < boundary.size(); ++ i)
        assert(boundary[i].size() + 1 == boundary_parameters[i].size());
    assert(validate_boundary_intersections(boundary_intersections));
#endif

#ifdef INFILL_DEBUG_OUTPUT
    static int iRun = 0;
    ++ iRun;
    int iStep = 0;
    export_infill_to_svg(boundary, boundary_parameters, boundary_intersections, infill, distance_colliding * 2, debug_out_path("%s-%03d.svg", "FillBase-mark_boundary_segments_touching_infill-start", iRun));
    Polylines perimeter_overlaps;
#endif // INFILL_DEBUG_OUTPUT

	EdgeGrid::Grid grid;
    // Make sure that the the grid is big enough for queries against the thick segment.
	grid.set_bbox(boundary_bbox.inflated(distance_colliding * 1.43));
	// Inflate the bounding box by a thick line width.
	grid.create(boundary, std::max(clip_distance, distance_colliding) + scale_(10.));

    // Visitor for the EdgeGrid to trim boundary_intersections with existing infill lines.
	struct Visitor {
		Visitor(const EdgeGrid::Grid &grid,
                const std::vector<Points> &boundary, const std::vector<std::vector<float>> &boundary_parameters, std::vector<std::vector<ContourIntersectionPoint*>> &boundary_intersections,
                const double radius) :
			grid(grid), boundary(boundary), boundary_parameters(boundary_parameters), boundary_intersections(boundary_intersections), radius(radius), trim_l_threshold(0.5 * radius) {}

        // Init with a segment of an infill line.
		void init(const Vec2d &infill_pt1, const Vec2d &infill_pt2) {
			this->infill_pt1 = &infill_pt1;
			this->infill_pt2 = &infill_pt2;
            this->infill_bbox.reset();
            this->infill_bbox.merge(infill_pt1);
            this->infill_bbox.merge(infill_pt2);
            this->infill_bbox.offset(this->radius + SCALED_EPSILON);
        }

		bool operator()(coord_t iy, coord_t ix) {
			// Called with a row and colum of the grid cell, which is intersected by a line.
			auto cell_data_range = this->grid.cell_data_range(iy, ix);
			for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++ it_contour_and_segment) {
				// End points of the line segment and their vector.
				auto segment = this->grid.segment(*it_contour_and_segment);
				const Vec2d seg_pt1 = segment.first.cast<double>();
				const Vec2d seg_pt2 = segment.second.cast<double>();
                std::pair<double, double> interval;
                BoundingBoxf bbox_seg;
                bbox_seg.merge(seg_pt1);
                bbox_seg.merge(seg_pt2);
#ifdef INFILL_DEBUG_OUTPUT
                //if (this->infill_bbox.overlap(bbox_seg)) this->perimeter_overlaps.push_back({ segment.first, segment.second });
#endif // INFILL_DEBUG_OUTPUT
                if (this->infill_bbox.overlap(bbox_seg) && line_rounded_thick_segment_collision(seg_pt1, seg_pt2, *this->infill_pt1, *this->infill_pt2, this->radius, interval)) {
                    // The boundary segment intersects with the infill segment thickened by radius.
                    // Interval is specified in Euclidian length from seg_pt1 to seg_pt2.
                    // 1) Find the Euclidian parameters of seg_pt1 and seg_pt2 on its boundary contour.
                    const std::vector<float> &contour_parameters = boundary_parameters[it_contour_and_segment->first];
                    const float contour_length = contour_parameters.back();
					const float param_seg_pt1  = contour_parameters[it_contour_and_segment->second];
#ifdef INFILL_DEBUG_OUTPUT
                    this->perimeter_overlaps.push_back({ Point((seg_pt1 + (seg_pt2 - seg_pt1).normalized() * interval.first).cast<coord_t>()),
                                                         Point((seg_pt1 + (seg_pt2 - seg_pt1).normalized() * interval.second).cast<coord_t>()) });
#endif // INFILL_DEBUG_OUTPUT
                    const float param_overlap1 = param_seg_pt1 + interval.first;
                    const float param_overlap2 = param_seg_pt1 + interval.second;
                    // 2) Find the ContourIntersectionPoints before param_overlap1 and after param_overlap2.
                    std::vector<ContourIntersectionPoint*> &intersections = boundary_intersections[it_contour_and_segment->first];
                    // Find the span of ContourIntersectionPoints, that is trimmed by the interval (param_overlap1, param_overlap2).
                    ContourIntersectionPoint *ip_low, *ip_high;
                    {
                        auto it_low  = Slic3r::lower_bound_by_predicate(intersections.begin(), intersections.end(), [param_overlap1](const ContourIntersectionPoint *l) { return l->param < param_overlap1; });
                        auto it_high = Slic3r::lower_bound_by_predicate(intersections.begin(), intersections.end(), [param_overlap2](const ContourIntersectionPoint *l) { return l->param < param_overlap2; });
                        ip_low  = it_low  == intersections.end() ? intersections.front() : *it_low;
                        ip_high = it_high == intersections.end() ? intersections.front() : *it_high;
                        if (ip_low->param != param_overlap1)
                            ip_low = ip_low->prev_on_contour;
                    }
                    assert(ip_low != ip_high);
                    // Verify that the interval (param_overlap1, param_overlap2) is inside the interval (ip_low->param, ip_high->param).
                    assert(cyclic_interval_inside_interval(ip_low->param, ip_high->param, param_overlap1, param_overlap2, contour_length));
                    assert(validate_boundary_intersections(boundary_intersections));
                    // Mark all ContourIntersectionPoints between ip_low and ip_high as consumed.
                    if (ip_low->next_on_contour != ip_high)
                        for (ContourIntersectionPoint *ip = ip_low->next_on_contour; ip != ip_high; ip = ip->next_on_contour) {
                            ip->consume_prev();
                            ip->consume_next();
                        }
                    // Subtract the interval from the first and last segments.
                    float trim_l = closed_contour_distance_ccw(ip_low->param, param_overlap1, contour_length);
                    //if (trim_l > trim_l_threshold)
                        ip_low->trim_next(trim_l);
                    trim_l = closed_contour_distance_ccw(param_overlap2, ip_high->param, contour_length);
                    //if (trim_l > trim_l_threshold)
                        ip_high->trim_prev(trim_l);
                    assert(ip_low->next_trimmed == ip_high->prev_trimmed);
                    assert(validate_boundary_intersections(boundary_intersections));
                    //FIXME mark point as consumed?
                    //FIXME verify the sequence between prev and next?
#ifdef INFILL_DEBUG_OUTPUT
					{
#if 0
                        static size_t iRun = 0;
						ExPolygon expoly(Polygon(*grid.contours().front()));
						for (size_t i = 1; i < grid.contours().size(); ++i)
							expoly.holes.emplace_back(Polygon(*grid.contours()[i]));
						SVG svg(debug_out_path("%s-%d.svg", "FillBase-mark_boundary_segments_touching_infill", iRun ++).c_str(), get_extents(expoly));
						svg.draw(expoly, "green");
						svg.draw(Line(segment.first, segment.second), "red");
						svg.draw(Line(this->infill_pt1->cast<coord_t>(), this->infill_pt2->cast<coord_t>()), "magenta");
#endif
                    }
#endif // INFILL_DEBUG_OUTPUT
				}
			}
			// Continue traversing the grid along the edge.
			return true;
		}

		const EdgeGrid::Grid 			   			        &grid;
		const std::vector<Points> 					        &boundary;
        const std::vector<std::vector<float>>               &boundary_parameters;
        std::vector<std::vector<ContourIntersectionPoint*>> &boundary_intersections;
		// Maximum distance between the boundary and the infill line allowed to consider the boundary not touching the infill line.
		const double								         radius;
        // Region around the contour / infill line intersection point, where the intersections are ignored.
        const float                                          trim_l_threshold;

		const Vec2d 								        *infill_pt1;
		const Vec2d 								        *infill_pt2;
        BoundingBoxf                                         infill_bbox;

#ifdef INFILL_DEBUG_OUTPUT
        Polylines                                            perimeter_overlaps;
#endif // INFILL_DEBUG_OUTPUT
	} visitor(grid, boundary, boundary_parameters, boundary_intersections, distance_colliding);

	for (const Polyline &polyline : infill) {
#ifdef INFILL_DEBUG_OUTPUT
        ++ iStep;
#endif // INFILL_DEBUG_OUTPUT
		// Clip the infill polyline by the Eucledian distance along the polyline.
		SegmentPoint start_point = clip_start_segment_and_point(polyline.points, clip_distance);
		SegmentPoint end_point   = clip_end_segment_and_point(polyline.points, clip_distance);
		if (start_point.valid() && end_point.valid() && 
			(start_point.idx_segment < end_point.idx_segment || (start_point.idx_segment == end_point.idx_segment && start_point.t < end_point.t))) {
			// The clipped polyline is non-empty.
#ifdef INFILL_DEBUG_OUTPUT
            visitor.perimeter_overlaps.clear();
#endif // INFILL_DEBUG_OUTPUT
			for (size_t point_idx = start_point.idx_segment; point_idx <= end_point.idx_segment; ++ point_idx) {
//FIXME extend the EdgeGrid to suport tracing a thick line.
#if 0
				Point pt1, pt2;
				Vec2d pt1d, pt2d;
				if (point_idx == start_point.idx_segment) {
					pt1d = start_point.point;
					pt1  = pt1d.cast<coord_t>();
				} else {
					pt1  = polyline.points[point_idx];
					pt1d = pt1.cast<double>();
				}
				if (point_idx == start_point.idx_segment) {
					pt2d = end_point.point;
					pt2  = pt1d.cast<coord_t>();
				} else {
					pt2  = polyline.points[point_idx];
					pt2d = pt2.cast<double>();
				}
				visitor.init(pt1d, pt2d);
				grid.visit_cells_intersecting_thick_line(pt1, pt2, distance_colliding, visitor);
#else
				Vec2d pt1 = (point_idx == start_point.idx_segment) ? start_point.point : polyline.points[point_idx    ].cast<double>();
				Vec2d pt2 = (point_idx == end_point  .idx_segment) ? end_point  .point : polyline.points[point_idx + 1].cast<double>();
#if 0
					{
						static size_t iRun = 0;
						ExPolygon expoly(Polygon(*grid.contours().front()));
						for (size_t i = 1; i < grid.contours().size(); ++i)
							expoly.holes.emplace_back(Polygon(*grid.contours()[i]));
						SVG svg(debug_out_path("%s-%d.svg", "FillBase-mark_boundary_segments_touching_infill0", iRun ++).c_str(), get_extents(expoly));
						svg.draw(expoly, "green");
						svg.draw(polyline, "blue");
						svg.draw(Line(pt1.cast<coord_t>(), pt2.cast<coord_t>()), "magenta", scale_(0.1));
					}
#endif
				visitor.init(pt1, pt2);
				// Simulate tracing of a thick line. This only works reliably if distance_colliding <= grid cell size.
				Vec2d v = (pt2 - pt1).normalized() * distance_colliding;
				Vec2d vperp = perp(v);
				Vec2d a = pt1 - v - vperp;
				Vec2d b = pt2 + v - vperp;
                assert(grid.bbox().contains(a.cast<coord_t>()));
                assert(grid.bbox().contains(b.cast<coord_t>()));
				grid.visit_cells_intersecting_line(a.cast<coord_t>(), b.cast<coord_t>(), visitor);
				a = pt1 - v + vperp;
				b = pt2 + v + vperp;
                assert(grid.bbox().contains(a.cast<coord_t>()));
                assert(grid.bbox().contains(b.cast<coord_t>()));
                grid.visit_cells_intersecting_line(a.cast<coord_t>(), b.cast<coord_t>(), visitor);
#endif
#ifdef INFILL_DEBUG_OUTPUT
//                export_infill_to_svg(boundary, boundary_parameters, boundary_intersections, infill, distance_colliding * 2, debug_out_path("%s-%03d-%03d-%03d.svg", "FillBase-mark_boundary_segments_touching_infill-step", iRun, iStep, int(point_idx)), { polyline });
#endif // INFILL_DEBUG_OUTPUT
			}
#ifdef INFILL_DEBUG_OUTPUT
            Polylines perimeter_overlaps;
            export_infill_to_svg(boundary, boundary_parameters, boundary_intersections, infill, distance_colliding * 2, debug_out_path("%s-%03d-%03d.svg", "FillBase-mark_boundary_segments_touching_infill-step", iRun, iStep), visitor.perimeter_overlaps, { polyline });
            append(perimeter_overlaps, std::move(visitor.perimeter_overlaps));
            perimeter_overlaps.clear();
#endif // INFILL_DEBUG_OUTPUT
        }
	}

#ifdef INFILL_DEBUG_OUTPUT
    export_infill_to_svg(boundary, boundary_parameters, boundary_intersections, infill, distance_colliding * 2, debug_out_path("%s-%03d.svg", "FillBase-mark_boundary_segments_touching_infill-end", iRun), perimeter_overlaps);
#endif // INFILL_DEBUG_OUTPUT
    assert(validate_boundary_intersections(boundary_intersections));
}

void Fill::connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary_src, Polylines &polylines_out, const double spacing, const FillParams &params)
{
	assert(! boundary_src.contour.points.empty());
    auto polygons_src = reserve_vector<const Polygon*>(boundary_src.holes.size() + 1);
    polygons_src.emplace_back(&boundary_src.contour);
    for (const Polygon &polygon : boundary_src.holes)
        polygons_src.emplace_back(&polygon);

    connect_infill(std::move(infill_ordered), polygons_src, get_extents(boundary_src.contour), polylines_out, spacing, params);
}

void Fill::connect_infill(Polylines &&infill_ordered, const Polygons &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params)
{
    auto polygons_src = reserve_vector<const Polygon*>(boundary_src.size());
    for (const Polygon &polygon : boundary_src)
        polygons_src.emplace_back(&polygon);

    connect_infill(std::move(infill_ordered), polygons_src, bbox, polylines_out, spacing, params);
}

void Fill::connect_infill(Polylines &&infill_ordered, const std::vector<const Polygon*> &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params)
{
	assert(! infill_ordered.empty());
    assert(params.anchor_length >= 0.01f);
    const auto anchor_length = float(scale_(params.anchor_length));

#if 0
    append(polylines_out, infill_ordered);
    return;
#endif

	// 1) Add the end points of infill_ordered to boundary_src.
	std::vector<Points>					   	boundary;
	std::vector<std::vector<float>>         boundary_params;
	boundary.assign(boundary_src.size(), Points());
	boundary_params.assign(boundary_src.size(), std::vector<float>());
	// Mapping the infill_ordered end point to a (contour, point) of boundary.
    static constexpr auto                   boundary_idx_unconnected = std::numeric_limits<size_t>::max();
	std::vector<ContourIntersectionPoint>   map_infill_end_point_to_boundary(infill_ordered.size() * 2, ContourIntersectionPoint{ boundary_idx_unconnected, boundary_idx_unconnected });
	{
		// Project the infill_ordered end points onto boundary_src.
		std::vector<std::pair<EdgeGrid::Grid::ClosestPointResult, size_t>> intersection_points;
		{
			EdgeGrid::Grid grid;
			grid.set_bbox(bbox.inflated(SCALED_EPSILON));
			grid.create(boundary_src, scale_(10.));
			intersection_points.reserve(infill_ordered.size() * 2);
			for (const Polyline &pl : infill_ordered)
				for (const Point *pt : { &pl.points.front(), &pl.points.back() }) {
					EdgeGrid::Grid::ClosestPointResult cp = grid.closest_point(*pt, SCALED_EPSILON);
					if (cp.valid()) {
						// The infill end point shall lie on the contour.
						assert(cp.distance < 2.);
						intersection_points.emplace_back(cp, (&pl - infill_ordered.data()) * 2 + (pt == &pl.points.front() ? 0 : 1));
					}
				}
			std::sort(intersection_points.begin(), intersection_points.end(), [](const std::pair<EdgeGrid::Grid::ClosestPointResult, size_t> &cp1, const std::pair<EdgeGrid::Grid::ClosestPointResult, size_t> &cp2) {
				return   cp1.first.contour_idx < cp2.first.contour_idx ||
						(cp1.first.contour_idx == cp2.first.contour_idx &&
							(cp1.first.start_point_idx < cp2.first.start_point_idx ||
								(cp1.first.start_point_idx == cp2.first.start_point_idx && cp1.first.t < cp2.first.t)));
			});
		}
		auto it = intersection_points.begin();
		auto it_end = intersection_points.end();
        std::vector<std::vector<ContourIntersectionPoint*>> boundary_intersection_points(boundary.size(), std::vector<ContourIntersectionPoint*>());
		for (size_t idx_contour = 0; idx_contour < boundary_src.size(); ++ idx_contour) {
            // Copy contour_src to contour_dst while adding intersection points.
            // Map infill end points map_infill_end_point_to_boundary to the newly inserted boundary points of contour_dst.
            // chain the points of map_infill_end_point_to_boundary along their respective contours.
			const Polygon &contour_src = *boundary_src[idx_contour];
			Points		  &contour_dst = boundary[idx_contour];
            std::vector<ContourIntersectionPoint*> &contour_intersection_points = boundary_intersection_points[idx_contour];
            ContourIntersectionPoint *pfirst = nullptr;
            ContourIntersectionPoint *pprev  = nullptr;
            {
                // Reserve intersection points.
                size_t n_intersection_points = 0;
                for (auto itx = it; itx != it_end && itx->first.contour_idx == idx_contour; ++ itx)
                    ++ n_intersection_points;
                contour_intersection_points.reserve(n_intersection_points);
            }
			for (size_t idx_point = 0; idx_point < contour_src.points.size(); ++ idx_point) {
				contour_dst.emplace_back(contour_src.points[idx_point]);
				for (; it != it_end && it->first.contour_idx == idx_contour && it->first.start_point_idx == idx_point; ++ it) {
					// Add these points to the destination contour.
                    const Polyline  &infill_line = infill_ordered[it->second / 2];
                    const Point     &pt          = (it->second & 1) ? infill_line.points.back() : infill_line.points.front();
#ifndef NDEBUG
                    {
					    const Vec2d pt1 = contour_src[idx_point].cast<double>();
					    const Vec2d pt2 = (idx_point + 1 == contour_src.size() ? contour_src.points.front() : contour_src.points[idx_point + 1]).cast<double>();
					    const Vec2d ptx = lerp(pt1, pt2, it->first.t);
                        assert(std::abs(pt.x() - pt.x()) < SCALED_EPSILON);
                        assert(std::abs(pt.y() - pt.y()) < SCALED_EPSILON);
                    }
#endif // NDEBUG
					map_infill_end_point_to_boundary[it->second] = ContourIntersectionPoint{ idx_contour, contour_dst.size() };
                    ContourIntersectionPoint *pthis = &map_infill_end_point_to_boundary[it->second];
                    if (pprev) {
                        pprev->next_on_contour = pthis;
                        pthis->prev_on_contour = pprev;                        
                    } else
                        pfirst = pthis;
                    contour_intersection_points.emplace_back(pthis);
                    pprev = pthis;
                    //add new point here
					contour_dst.emplace_back(pt);
				}
                if (pprev != pfirst) {
                    pprev->next_on_contour = pfirst;
                    pfirst->prev_on_contour = pprev;
                }
			}
			// Parametrize the new boundary with the intersection points inserted.
			std::vector<float> &contour_params = boundary_params[idx_contour];
			contour_params.assign(contour_dst.size() + 1, 0.f);
			for (size_t i = 1; i < contour_dst.size(); ++ i)
				contour_params[i] = contour_params[i - 1] + (contour_dst[i].cast<float>() - contour_dst[i - 1].cast<float>()).norm();
			contour_params.back() = contour_params[contour_params.size() - 2] + (contour_dst.back().cast<float>() - contour_dst.front().cast<float>()).norm();
            // Map parameters from contour_params to boundary_intersection_points.
            for (ContourIntersectionPoint *ip : contour_intersection_points)
                ip->param = contour_params[ip->point_idx];
            // and measure distance to the previous and next intersection point.
            const float contour_length = contour_params.back();
            for (ContourIntersectionPoint *ip : contour_intersection_points) {
                ip->contour_not_taken_length_prev = closed_contour_distance_ccw(ip->prev_on_contour->param, ip->param, contour_length);
                ip->contour_not_taken_length_next = closed_contour_distance_ccw(ip->param, ip->next_on_contour->param, contour_length);
            }
		}

		assert(boundary.size() == boundary_src.size());
#if 0
        // Adaptive Cubic Infill produces infill lines, which not always end at the outer boundary.
        assert(std::all_of(map_infill_end_point_to_boundary.begin(), map_infill_end_point_to_boundary.end(),
			[&boundary](const ContourIntersectionPoint &contour_point) {
				return contour_point.contour_idx < boundary.size() && contour_point.point_idx < boundary[contour_point.contour_idx].size();
			}));
#endif

        // Mark the points and segments of split boundary as consumed if they are very close to some of the infill line.
        {
            // @supermerill used 2. * scale_(spacing)
            const double clip_distance      = 1.7 * scale_(spacing);
            // Allow a bit of overlap. This value must be slightly higher than the overlap of FillAdaptive, otherwise
            // the anchors of the adaptive infill will mask the other side of the perimeter line.
            // (see connect_lines_using_hooks() in FillAdaptive.cpp)
            const double distance_colliding = 0.8 * scale_(spacing);
            mark_boundary_segments_touching_infill(boundary, boundary_params, boundary_intersection_points, bbox, infill_ordered, clip_distance, distance_colliding);
        }
	}

	// Connection from end of one infill line to the start of another infill line.
	//const float length_max = scale_(spacing);
//	const auto length_max = float(scale_((2. / params.density) * spacing));
	const auto length_max = float(scale_((1000. / params.density) * spacing));
	std::vector<size_t> merged_with(infill_ordered.size());
    std::iota(merged_with.begin(), merged_with.end(), 0);
	struct ConnectionCost {
		ConnectionCost(size_t idx_first, double cost, bool reversed) : idx_first(idx_first), cost(cost), reversed(reversed) {}
		size_t  idx_first;
		double  cost;
		bool 	reversed;
	};
	std::vector<ConnectionCost> connections_sorted;
	connections_sorted.reserve(infill_ordered.size() * 2 - 2);
	for (size_t idx_chain = 1; idx_chain < infill_ordered.size(); ++ idx_chain) {
		const Polyline 						&pl1 			= infill_ordered[idx_chain - 1];
		const Polyline 						&pl2 			= infill_ordered[idx_chain];
		const ContourIntersectionPoint		*cp1			= &map_infill_end_point_to_boundary[(idx_chain - 1) * 2 + 1];
		const ContourIntersectionPoint		*cp2			= &map_infill_end_point_to_boundary[idx_chain * 2];
		if (cp1->contour_idx != boundary_idx_unconnected && cp1->contour_idx == cp2->contour_idx) {
			// End points on the same contour. Try to connect them.
            std::pair<float, float> len = path_lengths_along_contour(cp1, cp2, boundary_params[cp1->contour_idx].back());
			if (len.first < length_max)
				connections_sorted.emplace_back(idx_chain - 1, len.first, false);
			if (len.second < length_max)
				connections_sorted.emplace_back(idx_chain - 1, len.second, true);
		}
	}
	std::sort(connections_sorted.begin(), connections_sorted.end(), [](const ConnectionCost& l, const ConnectionCost& r) { return l.cost < r.cost; });

    auto get_and_update_merged_with = [&merged_with](size_t polyline_idx) -> size_t {
        for (size_t last = polyline_idx;;) {
            size_t lower = merged_with[last];
            assert(lower <= last);
            if (lower == last) {
                merged_with[polyline_idx] = last;
                return last;
            }
            last = lower;
        }
        assert(false);
        return std::numeric_limits<size_t>::max();
    };

    const float line_half_width = 0.5f * scale_(spacing);

#if 0
    for (ConnectionCost &connection_cost : connections_sorted) {
		ContourIntersectionPoint *cp1    = &map_infill_end_point_to_boundary[connection_cost.idx_first * 2 + 1];
		ContourIntersectionPoint *cp2    = &map_infill_end_point_to_boundary[(connection_cost.idx_first + 1) * 2];
        assert(cp1 != cp2);
        assert(cp1->contour_idx == cp2->contour_idx && cp1->contour_idx != boundary_idx_unconnected);
        if (cp1->consumed || cp2->consumed)
            continue;
        const float               length = connection_cost.cost;
        bool                      could_connect;
        {
            // cp1, cp2 sorted CCW.
            ContourIntersectionPoint *cp_low  = connection_cost.reversed ? cp2 : cp1;
            ContourIntersectionPoint *cp_high = connection_cost.reversed ? cp1 : cp2;
            assert(std::abs(length - closed_contour_distance_ccw(cp_low->param, cp_high->param, boundary_params[cp1->contour_idx].back())) < SCALED_EPSILON);
            could_connect = ! cp_low->next_trimmed && ! cp_high->prev_trimmed;
            if (could_connect && cp_low->next_on_contour != cp_high) {
                // Other end of cp1, may or may not be on the same contour as cp1.
                const ContourIntersectionPoint *cp1prev = cp1 - 1;
                // Other end of cp2, may or may not be on the same contour as cp2.
                const ContourIntersectionPoint *cp2next = cp2 + 1;
                for (auto *cp = cp_low->next_on_contour; cp != cp_high; cp = cp->next_on_contour)
                    if (cp->consumed || cp == cp1prev || cp == cp2next || cp->prev_trimmed || cp->next_trimmed) {
                        could_connect = false;
                        break;
                    }
            }
        }
        // Indices of the polylines to be connected by a perimeter segment.
        size_t idx_first  = connection_cost.idx_first;
        size_t idx_second = idx_first + 1;
        idx_first = get_and_update_merged_with(idx_first);
        assert(idx_first < idx_second);
        assert(idx_second == merged_with[idx_second]);
        if (could_connect && length < anchor_length * 2.5) {
            // Take the complete contour.
            // Connect the two polygons using the boundary contour.
            take(infill_ordered[idx_first], infill_ordered[idx_second], boundary[cp1->contour_idx], cp1, cp2, connection_cost.reversed);
            // Mark the second polygon as merged with the first one.
            merged_with[idx_second] = merged_with[idx_first];
            infill_ordered[idx_second].points.clear();
        } else {
            // Try to connect cp1 resp. cp2 with a piece of perimeter line.
            take_limited(infill_ordered[idx_first],  boundary[cp1->contour_idx], boundary_params[cp1->contour_idx], cp1, cp2, connection_cost.reversed, anchor_length, line_half_width);
            take_limited(infill_ordered[idx_second], boundary[cp1->contour_idx], boundary_params[cp1->contour_idx], cp2, cp1, ! connection_cost.reversed, anchor_length, line_half_width);
        }
	}
#endif

    struct Arc {
        ContourIntersectionPoint    *intersection;
        float                        arc_length;
    };
    std::vector<Arc> arches;
    arches.reserve(map_infill_end_point_to_boundary.size());
    for (ContourIntersectionPoint &cp : map_infill_end_point_to_boundary)
        if (! cp.contour_idx != boundary_idx_unconnected && cp.next_on_contour != &cp && cp.could_connect_next())
            arches.push_back({ &cp, path_length_along_contour_ccw(&cp, cp.next_on_contour, boundary_params[cp.contour_idx].back()) });
    std::sort(arches.begin(), arches.end(), [](const auto &l, const auto &r) { return l.arc_length < r.arc_length; });

    for (Arc &arc : arches)
        if (! arc.intersection->consumed && ! arc.intersection->next_on_contour->consumed) {
            // Indices of the polylines to be connected by a perimeter segment.
            ContourIntersectionPoint *cp1            = arc.intersection;
            ContourIntersectionPoint *cp2            = arc.intersection->next_on_contour;
            size_t                    polyline_idx1  = get_and_update_merged_with(((cp1 - map_infill_end_point_to_boundary.data()) / 2));
            size_t                    polyline_idx2  = get_and_update_merged_with(((cp2 - map_infill_end_point_to_boundary.data()) / 2));
            const Points             &contour        = boundary[cp1->contour_idx];
            const std::vector<float> &contour_params = boundary_params[cp1->contour_idx];
            if (polyline_idx1 != polyline_idx2) {
                Polyline &polyline1 = infill_ordered[polyline_idx1];
                Polyline &polyline2 = infill_ordered[polyline_idx2];
                if (arc.arc_length < anchor_length * 2.5) {
                    // Not closing a loop, connecting the lines.
                    assert(contour[cp1->point_idx] == polyline1.points.front() || contour[cp1->point_idx] == polyline1.points.back());
                    if (contour[cp1->point_idx] == polyline1.points.front())
                        polyline1.reverse();
                    assert(contour[cp2->point_idx] == polyline2.points.front() || contour[cp2->point_idx] == polyline2.points.back());
                    if (contour[cp2->point_idx] == polyline2.points.back())
                        polyline2.reverse();
                    take(polyline1, polyline2, contour, cp1, cp2, false);
                    // Mark the second polygon as merged with the first one.
                    if (polyline_idx2 < polyline_idx1) {
                        polyline2 = std::move(polyline1);
                        polyline1.points.clear();
                        merged_with[polyline_idx1] = merged_with[polyline_idx2];
                    } else {
                        polyline2.points.clear();
                        merged_with[polyline_idx2] = merged_with[polyline_idx1];
                    }
                } else {
                    // Move along the perimeter, but don't take the whole arc.
                    take_limited(polyline1, contour, contour_params, cp1, cp2, false, anchor_length, line_half_width);
                    take_limited(polyline2, contour, contour_params, cp2, cp1, true,  anchor_length, line_half_width);
                }
            }
        }

    // Connect the remaining open infill lines to the perimeter lines if possible.
    for (ContourIntersectionPoint &contour_point : map_infill_end_point_to_boundary)
        if (! contour_point.consumed && contour_point.contour_idx != boundary_idx_unconnected) {
            const Points             &contour        = boundary[contour_point.contour_idx];
            const std::vector<float> &contour_params = boundary_params[contour_point.contour_idx];
            const size_t              contour_pt_idx = contour_point.point_idx;

            float     lprev         = contour_point.could_connect_prev() ?
                path_length_along_contour_ccw(contour_point.prev_on_contour, &contour_point, contour_params.back()) :
                std::numeric_limits<float>::max();
            float     lnext         = contour_point.could_connect_next() ?
                path_length_along_contour_ccw(&contour_point, contour_point.next_on_contour, contour_params.back()) :
                std::numeric_limits<float>::max();
            size_t    polyline_idx  = get_and_update_merged_with(((&contour_point - map_infill_end_point_to_boundary.data()) / 2));
            Polyline &polyline      = infill_ordered[polyline_idx];
            assert(! polyline.empty());
            assert(contour[contour_point.point_idx] == polyline.points.front() || contour[contour_point.point_idx] == polyline.points.back());
            bool connected = false;
            for (float l : { std::min(lprev, lnext), std::max(lprev, lnext) }) {
                if (l == std::numeric_limits<float>::max() || l > anchor_length * 2.5)
                    break;
                // Take the complete contour.
                bool      reversed      = l == lprev;
                ContourIntersectionPoint *cp2 = reversed ? contour_point.prev_on_contour : contour_point.next_on_contour;
                // Identify which end of the polyline touches the boundary.
                size_t    polyline_idx2 = get_and_update_merged_with(((cp2 - map_infill_end_point_to_boundary.data()) / 2));
                if (polyline_idx == polyline_idx2)
                    // Try the other side.
                    continue;
                // Not closing a loop.
                if (contour[contour_point.point_idx] == polyline.points.front())
                    polyline.reverse();
                Polyline &polyline2 = infill_ordered[polyline_idx2];
                assert(! polyline.empty());
                assert(contour[cp2->point_idx] == polyline2.points.front() || contour[cp2->point_idx] == polyline2.points.back());
                if (contour[cp2->point_idx] == polyline2.points.back())
                    polyline2.reverse();
                take(polyline, polyline2, contour, &contour_point, cp2, reversed);
                if (polyline_idx < polyline_idx2) {
                    // Mark the second polyline as merged with the first one.
                    merged_with[polyline_idx2] = polyline_idx;
                    polyline2.points.clear();
                } else {
                    // Mark the first polyline as merged with the second one.
                    merged_with[polyline_idx] = polyline_idx2;
                    polyline2 = std::move(polyline);
                    polyline.points.clear();
                }
                connected = true;
                break;
            }
            if (! connected) {
                // Which to take? One could optimize for:
                // 1) Shortest path
                // 2) Hook length
                // ...
                // Let's take the longer now, as this improves the chance of another hook to be placed on the other side of this contour point.
                float l = std::max(contour_point.contour_not_taken_length_prev, contour_point.contour_not_taken_length_next);
                if (l > SCALED_EPSILON) {
                    if (contour_point.contour_not_taken_length_prev > contour_point.contour_not_taken_length_next)
                        take_limited(polyline, contour, contour_params, &contour_point, contour_point.prev_on_contour, true, anchor_length, line_half_width);
                    else
                        take_limited(polyline, contour, contour_params, &contour_point, contour_point.next_on_contour, false, anchor_length, line_half_width);
                }
            }
        }

    polylines_out.reserve(polylines_out.size() + std::count_if(infill_ordered.begin(), infill_ordered.end(), [](const Polyline &pl) { return ! pl.empty(); }));
	for (Polyline &pl : infill_ordered)
		if (! pl.empty())
			polylines_out.emplace_back(std::move(pl));
}

} // namespace Slic3r
