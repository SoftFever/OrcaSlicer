#include <stdio.h>

#include "../ClipperUtils.hpp"
#include "../EdgeGrid.hpp"
#include "../Geometry.hpp"
#include "../Surface.hpp"
#include "../PrintConfig.hpp"
#include "../libslic3r.h"

#include "FillBase.hpp"
#include "FillConcentric.hpp"
#include "FillHoneycomb.hpp"
#include "Fill3DHoneycomb.hpp"
#include "FillGyroid.hpp"
#include "FillPlanePath.hpp"
#include "FillRectilinear.hpp"
#include "FillRectilinear2.hpp"
#include "FillRectilinear3.hpp"
#include "FillAdaptive.hpp"

namespace Slic3r {

Fill* Fill::new_from_type(const InfillPattern type)
{
    switch (type) {
    case ipConcentric:          return new FillConcentric();
    case ipHoneycomb:           return new FillHoneycomb();
    case ip3DHoneycomb:         return new Fill3DHoneycomb();
    case ipGyroid:              return new FillGyroid();
    case ipRectilinear:         return new FillRectilinear2();
    case ipMonotonous:          return new FillMonotonous();
    case ipLine:                return new FillLine();
    case ipGrid:                return new FillGrid2();
    case ipTriangles:           return new FillTriangles();
    case ipStars:               return new FillStars();
    case ipCubic:               return new FillCubic();
//  case ipGrid:                return new FillGrid();
    case ipArchimedeanChords:   return new FillArchimedeanChords();
    case ipHilbertCurve:        return new FillHilbertCurve();
    case ipOctagramSpiral:      return new FillOctagramSpiral();
    case ipAdaptiveCubic:       return new FillAdaptive();
    case ipSupportCubic:        return new FillSupportCubic();
    default: throw std::invalid_argument("unknown type");
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
            expp[i],
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

#if 0
// From pull request "Gyroid improvements" #2730 by @supermerill

/// cut poly between poly.point[idx_1] & poly.point[idx_1+1]
/// add p1+-width to one part and p2+-width to the other one.
/// add the "new" polyline to polylines (to part cut from poly)
/// p1 & p2 have to be between poly.point[idx_1] & poly.point[idx_1+1]
/// if idx_1 is ==0 or == size-1, then we don't need to create a new polyline.
static void cut_polyline(Polyline &poly, Polylines &polylines, size_t idx_1, Point p1, Point p2) {
    //reorder points
    if (p1.distance_to_square(poly.points[idx_1]) > p2.distance_to_square(poly.points[idx_1])) {
        Point temp = p2;
        p2 = p1;
        p1 = temp;
    }
    if (idx_1 == poly.points.size() - 1) {
        //shouldn't be possible.
        poly.points.erase(poly.points.end() - 1);
    } else {
        // create new polyline
        Polyline new_poly;
        //put points in new_poly
        new_poly.points.push_back(p2);
        new_poly.points.insert(new_poly.points.end(), poly.points.begin() + idx_1 + 1, poly.points.end());
        //erase&put points in poly
        poly.points.erase(poly.points.begin() + idx_1 + 1, poly.points.end());
        poly.points.push_back(p1);
        //safe test
        if (poly.length() == 0)
            poly.points = new_poly.points;
        else
            polylines.emplace_back(new_poly);
    }
}

/// the poly is like a polygon but with first_point != last_point (already removed)
static void cut_polygon(Polyline &poly, size_t idx_1, Point p1, Point p2) {
    //reorder points
    if (p1.distance_to_square(poly.points[idx_1]) > p2.distance_to_square(poly.points[idx_1])) {
        Point temp = p2;
        p2 = p1;
        p1 = temp;
    }
    //check if we need to rotate before cutting
    if (idx_1 != poly.size() - 1) {
        //put points in new_poly 
        poly.points.insert(poly.points.end(), poly.points.begin(), poly.points.begin() + idx_1 + 1);
        poly.points.erase(poly.points.begin(), poly.points.begin() + idx_1 + 1);
    }
    //put points in poly
    poly.points.push_back(p1);
    poly.points.insert(poly.points.begin(), p2);
}

/// check if the polyline from pts_to_check may be at 'width' distance of a point in polylines_blocker
/// it use equally_spaced_points with width/2 precision, so don't worry with pts_to_check number of points.
/// it use the given polylines_blocker points, be sure to put enough of them to be reliable.
/// complexity : N(pts_to_check.equally_spaced_points(width / 2)) x N(polylines_blocker.points)
static bool collision(const Points &pts_to_check, const Polylines &polylines_blocker, const coordf_t width) {
    //check if it's not too close to a polyline
    coordf_t min_dist_square = width * width * 0.9 - SCALED_EPSILON;
    Polyline better_polylines(pts_to_check);
    Points better_pts = better_polylines.equally_spaced_points(width / 2);
    for (const Point &p : better_pts) {
        for (const Polyline &poly2 : polylines_blocker) {
            for (const Point &p2 : poly2.points) {
                if (p.distance_to_square(p2) < min_dist_square) {
                    return true;
                }
            }
        }
    }
    return false;
}

/// Try to find a path inside polylines that allow to go from p1 to p2.
/// width if the width of the extrusion
/// polylines_blockers are the array of polylines to check if the path isn't blocked by something.
/// complexity: N(polylines.points) + a collision check after that if we finded a path: N(2(p2-p1)/width) x N(polylines_blocker.points)
static Points get_frontier(Polylines &polylines, const Point& p1, const Point& p2, const coord_t width, const Polylines &polylines_blockers, coord_t max_size = -1) {
    for (size_t idx_poly = 0; idx_poly < polylines.size(); ++idx_poly) {
        Polyline &poly = polylines[idx_poly];
        if (poly.size() <= 1) continue;

        //loop?
        if (poly.first_point() == poly.last_point()) {
            //polygon : try to find a line for p1 & p2.
            size_t idx_11, idx_12, idx_21, idx_22;
            idx_11 = poly.closest_point_index(p1);
            idx_12 = idx_11;
            if (Line(poly.points[idx_11], poly.points[(idx_11 + 1) % (poly.points.size() - 1)]).distance_to(p1) < SCALED_EPSILON) {
                idx_12 = (idx_11 + 1) % (poly.points.size() - 1);
            } else if (Line(poly.points[(idx_11 > 0) ? (idx_11 - 1) : (poly.points.size() - 2)], poly.points[idx_11]).distance_to(p1) < SCALED_EPSILON) {
                idx_11 = (idx_11 > 0) ? (idx_11 - 1) : (poly.points.size() - 2);
            } else {
                continue;
            }
            idx_21 = poly.closest_point_index(p2);
            idx_22 = idx_21;
            if (Line(poly.points[idx_21], poly.points[(idx_21 + 1) % (poly.points.size() - 1)]).distance_to(p2) < SCALED_EPSILON) {
                idx_22 = (idx_21 + 1) % (poly.points.size() - 1);
            } else if (Line(poly.points[(idx_21 > 0) ? (idx_21 - 1) : (poly.points.size() - 2)], poly.points[idx_21]).distance_to(p2) < SCALED_EPSILON) {
                idx_21 = (idx_21 > 0) ? (idx_21 - 1) : (poly.points.size() - 2);
            } else {
                continue;
            }


            //edge case: on the same line
            if (idx_11 == idx_21 && idx_12 == idx_22) {
                if (collision(Points() = { p1, p2 }, polylines_blockers, width)) return Points();
                //break loop
                poly.points.erase(poly.points.end() - 1);
                cut_polygon(poly, idx_11, p1, p2);
                return Points() = { Line(p1, p2).midpoint() };
            }

            //compute distance & array for the ++ path
            Points ret_1_to_2;
            double dist_1_to_2 = p1.distance_to(poly.points[idx_12]);
            ret_1_to_2.push_back(poly.points[idx_12]);
            size_t max = idx_12 <= idx_21 ? idx_21+1 : poly.points.size();
            for (size_t i = idx_12 + 1; i < max; i++) {
                dist_1_to_2 += poly.points[i - 1].distance_to(poly.points[i]);
                ret_1_to_2.push_back(poly.points[i]);
            }
            if (idx_12 > idx_21) {
                dist_1_to_2 += poly.points.back().distance_to(poly.points.front());
                ret_1_to_2.push_back(poly.points[0]);
                for (size_t i = 1; i <= idx_21; i++) {
                    dist_1_to_2 += poly.points[i - 1].distance_to(poly.points[i]);
                    ret_1_to_2.push_back(poly.points[i]);
                }
            }
            dist_1_to_2 += p2.distance_to(poly.points[idx_21]);

            //compute distance & array for the -- path
            Points ret_2_to_1;
            double dist_2_to_1 = p1.distance_to(poly.points[idx_11]);
            ret_2_to_1.push_back(poly.points[idx_11]);
            size_t min = idx_22 <= idx_11 ? idx_22 : 0;
            for (size_t i = idx_11; i > min; i--) {
                dist_2_to_1 += poly.points[i - 1].distance_to(poly.points[i]);
                ret_2_to_1.push_back(poly.points[i - 1]);
            }
            if (idx_22 > idx_11) {
                dist_2_to_1 += poly.points.back().distance_to(poly.points.front());
                ret_2_to_1.push_back(poly.points[poly.points.size() - 1]);
                for (size_t i = poly.points.size() - 1; i > idx_22; i--) {
                    dist_2_to_1 += poly.points[i - 1].distance_to(poly.points[i]);
                    ret_2_to_1.push_back(poly.points[i - 1]);
                }
            }
            dist_2_to_1 += p2.distance_to(poly.points[idx_22]);

            if (max_size < dist_2_to_1 && max_size < dist_1_to_2) {
                return Points();
            }

            //choose between the two direction (keep the short one)
            if (dist_1_to_2 < dist_2_to_1) {
                if (collision(ret_1_to_2, polylines_blockers, width)) return Points();
                //break loop
                poly.points.erase(poly.points.end() - 1);
                //remove points
                if (idx_12 <= idx_21) {
                    poly.points.erase(poly.points.begin() + idx_12, poly.points.begin() + idx_21 + 1);
                    if (idx_12 != 0) {
                        cut_polygon(poly, idx_11, p1, p2);
                    } //else : already cut at the good place
                } else {
                    poly.points.erase(poly.points.begin() + idx_12, poly.points.end());
                    poly.points.erase(poly.points.begin(), poly.points.begin() + idx_21);
                    cut_polygon(poly, poly.points.size() - 1, p1, p2);
                }
                return ret_1_to_2;
            } else {
                if (collision(ret_2_to_1, polylines_blockers, width)) return Points();
                //break loop
                poly.points.erase(poly.points.end() - 1);
                //remove points
                if (idx_22 <= idx_11) {
                    poly.points.erase(poly.points.begin() + idx_22, poly.points.begin() + idx_11 + 1);
                    if (idx_22 != 0) {
                        cut_polygon(poly, idx_21, p1, p2);
                    } //else : already cut at the good place
                } else {
                    poly.points.erase(poly.points.begin() + idx_22, poly.points.end());
                    poly.points.erase(poly.points.begin(), poly.points.begin() + idx_11);
                    cut_polygon(poly, poly.points.size() - 1, p1, p2);
                }
                return ret_2_to_1;
            }
        } else {
            //polyline : try to find a line for p1 & p2.
            size_t idx_1, idx_2;
            idx_1 = poly.closest_point_index(p1);
            if (idx_1 < poly.points.size() - 1 && Line(poly.points[idx_1], poly.points[idx_1 + 1]).distance_to(p1) < SCALED_EPSILON) {
            } else if (idx_1 > 0 && Line(poly.points[idx_1 - 1], poly.points[idx_1]).distance_to(p1) < SCALED_EPSILON) {
                idx_1 = idx_1 - 1;
            } else {
                continue;
            }
            idx_2 = poly.closest_point_index(p2);
            if (idx_2 < poly.points.size() - 1 && Line(poly.points[idx_2], poly.points[idx_2 + 1]).distance_to(p2) < SCALED_EPSILON) {
            } else if (idx_2 > 0 && Line(poly.points[idx_2 - 1], poly.points[idx_2]).distance_to(p2) < SCALED_EPSILON) {
                idx_2 = idx_2 - 1;
            } else {
                continue;
            }

            //edge case: on the same line
            if (idx_1 == idx_2) {
                if (collision(Points() = { p1, p2 }, polylines_blockers, width)) return Points();
                cut_polyline(poly, polylines, idx_1, p1, p2);
                return Points() = { Line(p1, p2).midpoint() };
            }

            //create ret array
            size_t first_idx = idx_1;
            size_t last_idx = idx_2 + 1;
            if (idx_1 > idx_2) {
                first_idx = idx_2;
                last_idx = idx_1 + 1;
            }
            Points p_ret;
            p_ret.insert(p_ret.end(), poly.points.begin() + first_idx + 1, poly.points.begin() + last_idx);

            coordf_t length = 0;
            for (size_t i = 1; i < p_ret.size(); i++) length += p_ret[i - 1].distance_to(p_ret[i]);

            if (max_size < length) {
                return Points();
            }

            if (collision(p_ret, polylines_blockers, width)) return Points();
            //cut polyline
            poly.points.erase(poly.points.begin() + first_idx + 1, poly.points.begin() + last_idx);
            cut_polyline(poly, polylines, first_idx, p1, p2);
            //order the returned array to be p1->p2
            if (idx_1 > idx_2) {
                std::reverse(p_ret.begin(), p_ret.end());
            }
            return p_ret;
        }

    }

    return Points();
}

/// Connect the infill_ordered polylines, in this order, from the back point to the next front point.
/// It uses only the boundary polygons to do so, and can't pass two times at the same place.
/// It avoid passing over the infill_ordered's polylines (preventing local over-extrusion).
/// return the connected polylines in polylines_out. Can output polygons (stored as polylines with first_point = last_point).
/// complexity: worst: N(infill_ordered.points) x N(boundary.points)
///             typical: N(infill_ordered) x ( N(boundary.points) + N(infill_ordered.points) )
void Fill::connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary, Polylines &polylines_out, const FillParams &params) {

    //TODO: fallback to the quick & dirty old algorithm when n(points) is too high.
    Polylines polylines_frontier = to_polylines(((Polygons)boundary));

    Polylines polylines_blocker;
    coord_t clip_size = scale_(this->spacing) * 2;
    for (const Polyline &polyline : infill_ordered) {
        if (polyline.length() > 2.01 * clip_size) {
            polylines_blocker.push_back(polyline);
            polylines_blocker.back().clip_end(clip_size);
            polylines_blocker.back().clip_start(clip_size);
        }
    }

    //length between two lines
    coordf_t ideal_length = (1 / params.density) * this->spacing;

    Polylines polylines_connected_first;
    bool first = true;
    for (const Polyline &polyline : infill_ordered) {
        if (!first) {
            // Try to connect the lines.
            Points &pts_end = polylines_connected_first.back().points;
            const Point &last_point = pts_end.back();
            const Point &first_point = polyline.points.front();
            if (last_point.distance_to(first_point) < scale_(this->spacing) * 10) {
                Points pts_frontier = get_frontier(polylines_frontier, last_point, first_point, scale_(this->spacing), polylines_blocker, (coord_t)scale_(ideal_length) * 2);
                if (!pts_frontier.empty()) {
                    // The lines can be connected.
                    pts_end.insert(pts_end.end(), pts_frontier.begin(), pts_frontier.end());
                    pts_end.insert(pts_end.end(), polyline.points.begin(), polyline.points.end());
                    continue;
                }
            }
        }
        // The lines cannot be connected.
        polylines_connected_first.emplace_back(std::move(polyline));

        first = false;
    }

    Polylines polylines_connected;
    first = true;
    for (const Polyline &polyline : polylines_connected_first) {
        if (!first) {
            // Try to connect the lines.
            Points &pts_end = polylines_connected.back().points;
            const Point &last_point = pts_end.back();
            const Point &first_point = polyline.points.front();

            Polylines before = polylines_frontier;
            Points pts_frontier = get_frontier(polylines_frontier, last_point, first_point, scale_(this->spacing), polylines_blocker);
            if (!pts_frontier.empty()) {
                // The lines can be connected.
                pts_end.insert(pts_end.end(), pts_frontier.begin(), pts_frontier.end());
                pts_end.insert(pts_end.end(), polyline.points.begin(), polyline.points.end());
                continue;
            }
        }
        // The lines cannot be connected.
        polylines_connected.emplace_back(std::move(polyline));

        first = false;
    }

    //try to link to nearest point if possible
    for (size_t idx1 = 0; idx1 < polylines_connected.size(); idx1++) {
        size_t min_idx = 0;
        coordf_t min_length = 0;
        bool switch_id1 = false;
        bool switch_id2 = false;
        for (size_t idx2 = idx1 + 1; idx2 < polylines_connected.size(); idx2++) {
            double last_first = polylines_connected[idx1].last_point().distance_to_square(polylines_connected[idx2].first_point());
            double first_first = polylines_connected[idx1].first_point().distance_to_square(polylines_connected[idx2].first_point());
            double first_last = polylines_connected[idx1].first_point().distance_to_square(polylines_connected[idx2].last_point());
            double last_last = polylines_connected[idx1].last_point().distance_to_square(polylines_connected[idx2].last_point());
            double min = std::min(std::min(last_first, last_last), std::min(first_first, first_last));
            if (min < min_length || min_length == 0) {
                min_idx = idx2;
                switch_id1 = (std::min(last_first, last_last) > std::min(first_first, first_last));
                switch_id2 = (std::min(last_first, first_first) > std::min(last_last, first_last));
                min_length = min;
            }
        }
        if (min_idx > idx1 && min_idx < polylines_connected.size()){
            Points pts_frontier = get_frontier(polylines_frontier, 
                switch_id1 ? polylines_connected[idx1].first_point() : polylines_connected[idx1].last_point(), 
                switch_id2 ? polylines_connected[min_idx].last_point() : polylines_connected[min_idx].first_point(),
                scale_(this->spacing), polylines_blocker);
            if (!pts_frontier.empty()) {
                if (switch_id1) polylines_connected[idx1].reverse();
                if (switch_id2) polylines_connected[min_idx].reverse();
                Points &pts_end = polylines_connected[idx1].points;
                pts_end.insert(pts_end.end(), pts_frontier.begin(), pts_frontier.end());
                pts_end.insert(pts_end.end(), polylines_connected[min_idx].points.begin(), polylines_connected[min_idx].points.end());
                polylines_connected.erase(polylines_connected.begin() + min_idx);
            }
        }
    }

    //try to create some loops if possible
    for (Polyline &polyline : polylines_connected) {
        Points pts_frontier = get_frontier(polylines_frontier, polyline.last_point(), polyline.first_point(), scale_(this->spacing), polylines_blocker);
        if (!pts_frontier.empty()) {
            polyline.points.insert(polyline.points.end(), pts_frontier.begin(), pts_frontier.end());
            polyline.points.insert(polyline.points.begin(), polyline.points.back());
        }
        polylines_out.emplace_back(polyline);
    }
}

#else

struct ContourPointData {
	ContourPointData(float param) : param(param) {}
	// Eucleidean position of the contour point along the contour.
	float param				= 0.f;
	// Was the segment starting with this contour point extruded?
	bool  segment_consumed	= false;
	// Was this point extruded over?
	bool  point_consumed	= false;
};

// Verify whether the contour from point idx_start to point idx_end could be taken (whether all segments along the contour were not yet extruded).
static bool could_take(const std::vector<ContourPointData> &contour_data, size_t idx_start, size_t idx_end)
{
	assert(idx_start != idx_end);
	for (size_t i = idx_start; i != idx_end; ) {
		if (contour_data[i].segment_consumed || contour_data[i].point_consumed)
			return false;
		if (++ i == contour_data.size())
			i = 0;
	}
	return ! contour_data[idx_end].point_consumed;
}

// Connect end of pl1 to the start of pl2 using the perimeter contour.
// The idx_start and idx_end are ordered so that the connecting polyline points will be taken with increasing indices.
static void take(Polyline &pl1, Polyline &&pl2, const Points &contour, std::vector<ContourPointData> &contour_data, size_t idx_start, size_t idx_end, bool reversed)
{
#ifndef NDEBUG
	size_t num_points_initial = pl1.points.size();
	assert(idx_start != idx_end);
#endif /* NDEBUG */

	{
		// Reserve memory at pl1 for the connecting contour and pl2.
		int new_points = int(idx_end) - int(idx_start) - 1;
		if (new_points < 0)
			new_points += int(contour.size());
		pl1.points.reserve(pl1.points.size() + size_t(new_points) + pl2.points.size());
	}

	contour_data[idx_start].point_consumed   = true;
	contour_data[idx_start].segment_consumed = true;
	contour_data[idx_end  ].point_consumed   = true;

	if (reversed) {
		size_t i = (idx_end == 0) ? contour_data.size() - 1 : idx_end - 1;
		while (i != idx_start) {
			contour_data[i].point_consumed   = true;
			contour_data[i].segment_consumed = true;
			pl1.points.emplace_back(contour[i]);
			if (i == 0)
				i = contour_data.size();
			-- i;
		}
	} else {
		size_t i = idx_start;
		if (++ i == contour_data.size())
			i = 0;
		while (i != idx_end) {
			contour_data[i].point_consumed   = true;
			contour_data[i].segment_consumed = true;
			pl1.points.emplace_back(contour[i]);
			if (++ i == contour_data.size())
				i = 0;
		}
	}

	append(pl1.points, std::move(pl2.points));
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
	        double l2 = v.squaredNorm();
	        if (l2 > distance * distance) {
	        	out.idx_segment = i;
	        	out.t 			= distance / sqrt(l2);
	        	out.point 		= pt_prev + out.t * v;
	            break;
	        }
	        distance -= sqrt(l2);
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
	        double l2 = v.squaredNorm();
	        if (l2 > distance * distance) {
	        	out.idx_segment = i;
	        	out.t 			= distance / sqrt(l2);
	        	out.point 		= pt_next + out.t * v;
				// Store the parameter referenced to the starting point of a segment.
				out.t			= 1. - out.t;
	            break;
	        }
	        distance -= sqrt(l2);
	        pt_next = pt;
	    }
	}
	return out;
}

// Optimized version with the precalculated v1 = p1b - p1a and l1_2 = v1.squaredNorm().
// Assumption: l1_2 < EPSILON.
static inline double segment_point_distance_squared(const Vec2d &p1a, const Vec2d &p1b, const Vec2d &v1, const double l1_2, const Vec2d &p2)
{
	assert(l1_2 > EPSILON);
	Vec2d  v12 = p2 - p1a;
	double t   = v12.dot(v1);
	return (t <= 0.  ) ? v12.squaredNorm() :
	       (t >= l1_2) ? (p2 - p1a).squaredNorm() :
		   ((t / l1_2) * v1 - v12).squaredNorm();
}

static inline double segment_point_distance_squared(const Vec2d &p1a, const Vec2d &p1b, const Vec2d &p2)
{
    const Vec2d  v  = p1b - p1a;
    const double l2 = v.squaredNorm();
    if (l2 < EPSILON)
        // p1a == p1b
        return (p2  - p1a).squaredNorm();
    return segment_point_distance_squared(p1a, p1b, v, v.squaredNorm(), p2);
}

// Distance to the closest point of line.
static inline double min_distance_of_segments(const Vec2d &p1a, const Vec2d &p1b, const Vec2d &p2a, const Vec2d &p2b)
{
    Vec2d   v1 		= p1b - p1a;
    double  l1_2 	= v1.squaredNorm();
    if (l1_2 < EPSILON)
        // p1a == p1b: Return distance of p1a from the (p2a, p2b) segment.
        return segment_point_distance_squared(p2a, p2b, p1a);

    Vec2d   v2 		= p2b - p2a;
    double  l2_2 	= v2.squaredNorm();
    if (l2_2 < EPSILON)
        // p2a == p2b: Return distance of p2a from the (p1a, p1b) segment.
        return segment_point_distance_squared(p1a, p1b, v1, l1_2, p2a);

    return std::min(
		std::min(segment_point_distance_squared(p1a, p1b, v1, l1_2, p2a), segment_point_distance_squared(p1a, p1b, v1, l1_2, p2b)),
		std::min(segment_point_distance_squared(p2a, p2b, v2, l2_2, p1a), segment_point_distance_squared(p2a, p2b, v2, l2_2, p1b)));
}

// Mark the segments of split boundary as consumed if they are very close to some of the infill line.
void mark_boundary_segments_touching_infill(
	const std::vector<Points> 					&boundary,
	std::vector<std::vector<ContourPointData>> 	&boundary_data,
	const BoundingBox 							&boundary_bbox,
	const Polylines 							&infill,
	const double							     clip_distance,
	const double 								 distance_colliding)
{
	EdgeGrid::Grid grid;
	grid.set_bbox(boundary_bbox);
	// Inflate the bounding box by a thick line width.
	grid.create(boundary, clip_distance + scale_(10.));

	struct Visitor {
		Visitor(const EdgeGrid::Grid &grid, const std::vector<Points> &boundary, std::vector<std::vector<ContourPointData>> &boundary_data, const double dist2_max) :
			grid(grid), boundary(boundary), boundary_data(boundary_data), dist2_max(dist2_max) {}

		void init(const Vec2d &pt1, const Vec2d &pt2) {
			this->pt1 = &pt1;
			this->pt2 = &pt2;
		}

		bool operator()(coord_t iy, coord_t ix) {
			// Called with a row and colum of the grid cell, which is intersected by a line.
			auto cell_data_range = this->grid.cell_data_range(iy, ix);
			for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++ it_contour_and_segment) {
				// End points of the line segment and their vector.
				auto segment = this->grid.segment(*it_contour_and_segment);
				const Vec2d seg_pt1 = segment.first.cast<double>();
				const Vec2d seg_pt2 = segment.second.cast<double>();
				if (min_distance_of_segments(seg_pt1, seg_pt2, *this->pt1, *this->pt2) < this->dist2_max) {
					// Mark this boundary segment as touching the infill line.
					ContourPointData &bdp = boundary_data[it_contour_and_segment->first][it_contour_and_segment->second];
					bdp.segment_consumed = true;
					// There is no need for checking seg_pt2 as it will be checked the next time.
					bool point_touching = false;
					if (segment_point_distance_squared(*this->pt1, *this->pt2, seg_pt1) < this->dist2_max) {
						point_touching = true;
						bdp.point_consumed = true;
					}
#if 0
					{
						static size_t iRun = 0;
						ExPolygon expoly(Polygon(*grid.contours().front()));
						for (size_t i = 1; i < grid.contours().size(); ++i)
							expoly.holes.emplace_back(Polygon(*grid.contours()[i]));
						SVG svg(debug_out_path("%s-%d.svg", "FillBase-mark_boundary_segments_touching_infill", iRun ++).c_str(), get_extents(expoly));
						svg.draw(expoly, "green");
						svg.draw(Line(segment.first, segment.second), "red");
						svg.draw(Line(this->pt1->cast<coord_t>(), this->pt2->cast<coord_t>()), "magenta");
					}
#endif
				}
			}
			// Continue traversing the grid along the edge.
			return true;
		}

		const EdgeGrid::Grid 			   			&grid;
		const std::vector<Points> 					&boundary;
		std::vector<std::vector<ContourPointData>> 	&boundary_data;
		// Maximum distance between the boundary and the infill line allowed to consider the boundary not touching the infill line.
		const double								 dist2_max;

		const Vec2d 								*pt1;
		const Vec2d 								*pt2;
	} visitor(grid, boundary, boundary_data, distance_colliding * distance_colliding);

	BoundingBoxf bboxf(boundary_bbox.min.cast<double>(), boundary_bbox.max.cast<double>());
	bboxf.offset(- SCALED_EPSILON);

	for (const Polyline &polyline : infill) {
		// Clip the infill polyline by the Eucledian distance along the polyline.
		SegmentPoint start_point = clip_start_segment_and_point(polyline.points, clip_distance);
		SegmentPoint end_point   = clip_end_segment_and_point(polyline.points, clip_distance);
		if (start_point.valid() && end_point.valid() && 
			(start_point.idx_segment < end_point.idx_segment || (start_point.idx_segment == end_point.idx_segment && start_point.t < end_point.t))) {
			// The clipped polyline is non-empty.
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
				Vec2d vperp(-v.y(), v.x());
				Vec2d a = pt1 - v - vperp;
				Vec2d b = pt1 + v - vperp;
				if (Geometry::liang_barsky_line_clipping(a, b, bboxf))
					grid.visit_cells_intersecting_line(a.cast<coord_t>(), b.cast<coord_t>(), visitor);
				a = pt1 - v + vperp;
				b = pt1 + v + vperp;
				if (Geometry::liang_barsky_line_clipping(a, b, bboxf))
					grid.visit_cells_intersecting_line(a.cast<coord_t>(), b.cast<coord_t>(), visitor);
#endif
			}
		}
	}
}

void Fill::connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary_src, Polylines &polylines_out, const double spacing, const FillParams &params)
{
	assert(! infill_ordered.empty());
	assert(! boundary_src.contour.points.empty());

	BoundingBox bbox = get_extents(boundary_src.contour);
	bbox.offset(SCALED_EPSILON);

	// 1) Add the end points of infill_ordered to boundary_src.
	std::vector<Points>					   		boundary;
	std::vector<std::vector<ContourPointData>> 	boundary_data;
	boundary.assign(boundary_src.holes.size() + 1, Points());
	boundary_data.assign(boundary_src.holes.size() + 1, std::vector<ContourPointData>());
	// Mapping the infill_ordered end point to a (contour, point) of boundary.
	std::vector<std::pair<size_t, size_t>> map_infill_end_point_to_boundary;
	map_infill_end_point_to_boundary.assign(infill_ordered.size() * 2, std::pair<size_t, size_t>(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()));
	{
		// Project the infill_ordered end points onto boundary_src.
		std::vector<std::pair<EdgeGrid::Grid::ClosestPointResult, size_t>> intersection_points;
		{
			EdgeGrid::Grid grid;
			grid.set_bbox(bbox);
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
		for (size_t idx_contour = 0; idx_contour <= boundary_src.holes.size(); ++ idx_contour) {
			const Polygon &contour_src = (idx_contour == 0) ? boundary_src.contour : boundary_src.holes[idx_contour - 1];
			Points		  &contour_dst = boundary[idx_contour];
			for (size_t idx_point = 0; idx_point < contour_src.points.size(); ++ idx_point) {
				contour_dst.emplace_back(contour_src.points[idx_point]);
				for (; it != it_end && it->first.contour_idx == idx_contour && it->first.start_point_idx == idx_point; ++ it) {
					// Add these points to the destination contour.
					const Vec2d pt1 = contour_src[idx_point].cast<double>();
					const Vec2d pt2 = (idx_point + 1 == contour_src.size() ? contour_src.points.front() : contour_src.points[idx_point + 1]).cast<double>();
					const Vec2d pt  = lerp(pt1, pt2, it->first.t);
					map_infill_end_point_to_boundary[it->second] = std::make_pair(idx_contour, contour_dst.size());
					contour_dst.emplace_back(pt.cast<coord_t>());
				}
			}
			// Parametrize the curve.
			std::vector<ContourPointData> &contour_data = boundary_data[idx_contour];
			contour_data.reserve(contour_dst.size());
			contour_data.emplace_back(ContourPointData(0.f));
			for (size_t i = 1; i < contour_dst.size(); ++ i)
				contour_data.emplace_back(contour_data.back().param + (contour_dst[i].cast<float>() - contour_dst[i - 1].cast<float>()).norm());
			contour_data.front().param = contour_data.back().param + (contour_dst.back().cast<float>() - contour_dst.front().cast<float>()).norm();
		}

#ifndef NDEBUG
		assert(boundary.size() == boundary_src.num_contours());
		assert(std::all_of(map_infill_end_point_to_boundary.begin(), map_infill_end_point_to_boundary.end(),
			[&boundary](const std::pair<size_t, size_t> &contour_point) {
				return contour_point.first < boundary.size() && contour_point.second < boundary[contour_point.first].size();
			}));
#endif /* NDEBUG */
	}

	// Mark the points and segments of split boundary as consumed if they are very close to some of the infill line.
	{
		// @supermerill used 2. * scale_(spacing)
		const double clip_distance		= 3. * scale_(spacing);
		const double distance_colliding = 1.1 * scale_(spacing);
		mark_boundary_segments_touching_infill(boundary, boundary_data, bbox, infill_ordered, clip_distance, distance_colliding);
	}

	// Connection from end of one infill line to the start of another infill line.
	//const float length_max = scale_(spacing);
//	const float length_max = scale_((2. / params.density) * spacing);
	const float length_max = scale_((1000. / params.density) * spacing);
	std::vector<size_t> merged_with(infill_ordered.size());
	for (size_t i = 0; i < merged_with.size(); ++ i)
		merged_with[i] = i;
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
		const std::pair<size_t, size_t>		*cp1			= &map_infill_end_point_to_boundary[(idx_chain - 1) * 2 + 1];
		const std::pair<size_t, size_t>		*cp2			= &map_infill_end_point_to_boundary[idx_chain * 2];
		const std::vector<ContourPointData>	&contour_data	= boundary_data[cp1->first];
		if (cp1->first == cp2->first) {
			// End points on the same contour. Try to connect them.
			float param_lo  = (cp1->second == 0) ? 0.f : contour_data[cp1->second].param;
			float param_hi  = (cp2->second == 0) ? 0.f : contour_data[cp2->second].param;
			float param_end = contour_data.front().param;
			bool  reversed  = false;
			if (param_lo > param_hi) {
				std::swap(param_lo, param_hi);
				reversed = true;
			}
			assert(param_lo >= 0.f && param_lo <= param_end);
			assert(param_hi >= 0.f && param_hi <= param_end);
			double len = param_hi - param_lo;
			if (len < length_max)
				connections_sorted.emplace_back(idx_chain - 1, len, reversed);
			len = param_lo + param_end - param_hi;
			if (len < length_max)
				connections_sorted.emplace_back(idx_chain - 1, len, ! reversed);
		}
	}
	std::sort(connections_sorted.begin(), connections_sorted.end(), [](const ConnectionCost& l, const ConnectionCost& r) { return l.cost < r.cost; });

	size_t idx_chain_last = 0;
	for (ConnectionCost &connection_cost : connections_sorted) {
		const std::pair<size_t, size_t>	*cp1     = &map_infill_end_point_to_boundary[connection_cost.idx_first * 2 + 1];
		const std::pair<size_t, size_t>	*cp1prev = cp1 - 1;
		const std::pair<size_t, size_t>	*cp2     = &map_infill_end_point_to_boundary[(connection_cost.idx_first + 1) * 2];
		const std::pair<size_t, size_t>	*cp2next = cp2 + 1;
		assert(cp1->first == cp2->first);
		std::vector<ContourPointData>	&contour_data = boundary_data[cp1->first];
		if (connection_cost.reversed)
			std::swap(cp1, cp2);
		// Mark the the other end points of the segments to be taken as consumed temporarily, so they will not be crossed
		// by the new connection line.
		bool prev_marked = false;
		bool next_marked = false;
		if (cp1prev->first == cp1->first && ! contour_data[cp1prev->second].point_consumed) {
			contour_data[cp1prev->second].point_consumed = true;
			prev_marked = true;
		}
		if (cp2next->first == cp1->first && ! contour_data[cp2next->second].point_consumed) {
			contour_data[cp2next->second].point_consumed = true;
			next_marked = true;
		}
		if (could_take(contour_data, cp1->second, cp2->second)) {
			// Indices of the polygons to be connected.
			size_t idx_first  = connection_cost.idx_first;
			size_t idx_second = idx_first + 1;
			for (size_t last = idx_first;;) {
				size_t lower = merged_with[last];
				if (lower == last) {
					merged_with[idx_first] = lower;
					idx_first = lower;
					break;
				}
				last = lower;
			}
			// Connect the two polygons using the boundary contour.
			take(infill_ordered[idx_first], std::move(infill_ordered[idx_second]), boundary[cp1->first], contour_data, cp1->second, cp2->second, connection_cost.reversed);
			// Mark the second polygon as merged with the first one.
			merged_with[idx_second] = merged_with[idx_first];
		}
		if (prev_marked)
			contour_data[cp1prev->second].point_consumed = false;
		if (next_marked)
			contour_data[cp2next->second].point_consumed = false;
	}
	polylines_out.reserve(polylines_out.size() + std::count_if(infill_ordered.begin(), infill_ordered.end(), [](const Polyline &pl) { return ! pl.empty(); }));
	for (Polyline &pl : infill_ordered)
		if (! pl.empty())
			polylines_out.emplace_back(std::move(pl));
}

#endif

} // namespace Slic3r
