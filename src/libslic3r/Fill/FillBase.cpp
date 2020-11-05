#include <stdio.h>

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
#include "FillRectilinear.hpp"
#include "FillRectilinear2.hpp"
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
    case ipMonotonic:           return new FillMonotonic();
    case ipLine:                return new FillLine();
    case ipGrid:                return new FillGrid2();
    case ipTriangles:           return new FillTriangles();
    case ipStars:               return new FillStars();
    case ipCubic:               return new FillCubic();
//  case ipGrid:                return new FillGrid();
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
    // Zero'th param is the length of the contour.
    float param_lo  = cp1->param;
    float param_hi  = cp2->param;
    assert(param_lo >= 0.f && param_lo <= contour_length);
    assert(param_hi >= 0.f && param_hi <= contour_length);
    return cp1 < cp2 ? param_hi - param_lo : param_lo + contour_length - param_hi;
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
            if (cp->prev_on_contour->consumed)
                // Don't overlap with an already extruded infill line.
                length_to_go = std::max(0.f, std::min(length_to_go, l - line_half_width));
            cp->consume_prev();
            if (l >= length_to_go) {
                cp->prev_on_contour->trim_next(l - length_to_go);
                take_cw_limited(pl1, contour, params, cp->point_idx, cp->prev_on_contour->point_idx, length_to_go);
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
            if (cp->next_on_contour->consumed)
                // Don't overlap with an already extruded infill line.
                length_to_go = std::max(0.f, std::min(length_to_go, l - line_half_width));
            cp->consume_next();
            if (l >= length_to_go) {
                cp->next_on_contour->trim_prev(l - length_to_go);
                take_ccw_limited(pl1, contour, params, cp->point_idx, cp->next_on_contour->point_idx, length_to_go);
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
            intersects  = (segment_a - lpt).squaredNorm() < offset2;
            intersects |= (segment_b - lpt).squaredNorm() < offset2;
            if (! intersects) {

            }
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
            std::pair<double, double> interval;
            if (Geometry::liang_barsky_line_clipping_interval(
                    Vec2d(line_a - segment_a), 
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
#endif

	EdgeGrid::Grid grid;
	grid.set_bbox(boundary_bbox);
	// Inflate the bounding box by a thick line width.
	grid.create(boundary, std::max(clip_distance, distance_colliding) + scale_(10.));

    // Visitor for the EdgeGrid to trim boundary_intersections with existing infill lines.
	struct Visitor {
		Visitor(const EdgeGrid::Grid &grid,
                const std::vector<Points> &boundary, const std::vector<std::vector<float>> &boundary_parameters, std::vector<std::vector<ContourIntersectionPoint*>> &boundary_intersections,
                const double radius) :
			grid(grid), boundary(boundary), boundary_parameters(boundary_parameters), boundary_intersections(boundary_intersections), radius(radius) {}

        // Init with a segment of an infill line.
		void init(const Vec2d &infill_pt1, const Vec2d &infill_pt2) {
			this->infill_pt1 = &infill_pt1;
			this->infill_pt2 = &infill_pt2;
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
                if (line_rounded_thick_segment_collision(seg_pt1, seg_pt2, *this->infill_pt1, *this->infill_pt2, this->radius, interval)) {
                    // The boundary segment intersects with the infill segment thickened by radius.
                    // Interval is specified in Euclidian length from seg_pt1 to seg_pt2.
                    // 1) Find the Euclidian parameters of seg_pt1 and seg_pt2 on its boundary contour.
                    const std::vector<float> &contour_parameters = boundary_parameters[it_contour_and_segment->first];
                    const float contour_length = contour_parameters.back();
					const float param_seg_pt1  = contour_parameters[it_contour_and_segment->second];
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
                    // Mark all ContourIntersectionPoints between ip_low and ip_high as consumed.
                    if (ip_low->next_on_contour != ip_high)
                        for (ContourIntersectionPoint *ip = ip_low->next_on_contour; ip->next_on_contour != ip_high; ip = ip->next_on_contour) {
                            ip->consume_prev();
                            ip->consume_next();
                        }
                    // Subtract the interval from the first and last segments.
                    ip_low->trim_next(closed_contour_distance_ccw(ip_low->param, param_overlap1, contour_length));
                    ip_high->trim_prev(closed_contour_distance_ccw(param_overlap2, ip_high->param, contour_length));
                    //FIXME mark point as consumed?
                    //FIXME verify the sequence between prev and next?
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

		const EdgeGrid::Grid 			   			        &grid;
		const std::vector<Points> 					        &boundary;
        const std::vector<std::vector<float>>               &boundary_parameters;
        std::vector<std::vector<ContourIntersectionPoint*>> &boundary_intersections;
		// Maximum distance between the boundary and the infill line allowed to consider the boundary not touching the infill line.
		const double								         radius;

		const Vec2d 								        *infill_pt1;
		const Vec2d 								        *infill_pt2;
	} visitor(grid, boundary, boundary_parameters, boundary_intersections, distance_colliding);

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

void Fill::connect_infill(Polylines &&infill_ordered, const ExPolygon &boundary_src, Polylines &polylines_out, const double spacing, const FillParams &params, const int hook_length)
{
	assert(! boundary_src.contour.points.empty());
    BoundingBox bbox = get_extents(boundary_src.contour);
    bbox.offset(SCALED_EPSILON);

    auto polygons_src = reserve_vector<const Polygon*>(boundary_src.holes.size() + 1);
    polygons_src.emplace_back(&boundary_src.contour);
    for (const Polygon &polygon : boundary_src.holes)
        polygons_src.emplace_back(&polygon);

    connect_infill(std::move(infill_ordered), polygons_src, bbox, polylines_out, spacing, params, hook_length);
}

void Fill::connect_infill(Polylines &&infill_ordered, const Polygons &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params, const int hook_length)
{
    auto polygons_src = reserve_vector<const Polygon*>(boundary_src.size());
    for (const Polygon &polygon : boundary_src)
        polygons_src.emplace_back(&polygon);

    connect_infill(std::move(infill_ordered), polygons_src, bbox, polylines_out, spacing, params, hook_length);
}

void Fill::connect_infill(Polylines &&infill_ordered, const std::vector<const Polygon*> &boundary_src, const BoundingBox &bbox, Polylines &polylines_out, const double spacing, const FillParams &params, const int hook_length)
{
	assert(! infill_ordered.empty());

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
#ifndef NDEBUG
                    const Polyline  &infill_line = infill_ordered[it->second / 2];
                    const Point     &pt          = (it->second & 1) ? infill_line.points.back() : infill_line.points.front();
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
            const double clip_distance      = 3. * scale_(spacing);
            const double distance_colliding = 1.1 * scale_(spacing);
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

    const float take_max_length = hook_length > 0.f ? hook_length : std::numeric_limits<float>::max();
    const float line_half_width = 0.5f * scale_(spacing);
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
            if (! could_connect && cp_low->next_on_contour != cp_high) {
                // Other end of cp1, may or may not be on the same contour as cp1.
                const ContourIntersectionPoint* cp1prev = cp1 - 1;
                // Other end of cp2, may or may not be on the same contour as cp2.
                const ContourIntersectionPoint* cp2next = cp2 + 1;
                for (auto *cp = cp_low->next_on_contour; cp->next_on_contour != cp_high; cp = cp->next_on_contour) {
                    if (cp->consumed || cp == cp1prev || cp == cp2next || cp->prev_trimmed || cp->next_trimmed) {
                        could_connect = false;
                        break;
                    }
                }
            }
        }
        // Indices of the polylines to be connected by a perimeter segment.
        size_t idx_first  = connection_cost.idx_first;
        size_t idx_second = idx_first + 1;
        idx_first = get_and_update_merged_with(idx_first);
        assert(idx_first < idx_second);
        assert(idx_second == merged_with[idx_second]);
        if (could_connect && (hook_length == 0.f || length < hook_length * 2.5)) {
            // Take the complete contour.
            // Connect the two polygons using the boundary contour.
            take(infill_ordered[idx_first], infill_ordered[idx_second], boundary[cp1->contour_idx], cp1, cp2, connection_cost.reversed);
            // Mark the second polygon as merged with the first one.
            merged_with[idx_second] = merged_with[idx_first];
            infill_ordered[idx_second].points.clear();
        } else {
            // Try to connect cp1 resp. cp2 with a piece of perimeter line.
            take_limited(infill_ordered[idx_first],  boundary[cp1->contour_idx], boundary_params[cp1->contour_idx], cp1, cp2, connection_cost.reversed, take_max_length, line_half_width);
            take_limited(infill_ordered[idx_second], boundary[cp1->contour_idx], boundary_params[cp1->contour_idx], cp2, cp1, ! connection_cost.reversed, take_max_length, line_half_width);
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
                if (l == std::numeric_limits<float>::max() || (hook_length > 0.f && l > hook_length * 2.5))
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
                        take_limited(polyline, contour, contour_params, &contour_point, contour_point.prev_on_contour, true, take_max_length, line_half_width);
                    else
                        take_limited(polyline, contour, contour_params, &contour_point, contour_point.next_on_contour, false, take_max_length, line_half_width);
                }
            }
        }

    polylines_out.reserve(polylines_out.size() + std::count_if(infill_ordered.begin(), infill_ordered.end(), [](const Polyline &pl) { return ! pl.empty(); }));
	for (Polyline &pl : infill_ordered)
		if (! pl.empty())
			polylines_out.emplace_back(std::move(pl));
}

#endif

} // namespace Slic3r
