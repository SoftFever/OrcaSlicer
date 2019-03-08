#include <stdio.h>

#include "../ClipperUtils.hpp"
#include "../Surface.hpp"
#include "../PrintConfig.hpp"

#include "FillBase.hpp"
#include "FillConcentric.hpp"
#include "FillHoneycomb.hpp"
#include "Fill3DHoneycomb.hpp"
#include "FillGyroid.hpp"
#include "FillPlanePath.hpp"
#include "FillRectilinear.hpp"
#include "FillRectilinear2.hpp"
#include "FillRectilinear3.hpp"

namespace Slic3r {

Fill* Fill::new_from_type(const InfillPattern type)
{
    switch (type) {
    case ipConcentric:          return new FillConcentric();
    case ipHoneycomb:           return new FillHoneycomb();
    case ip3DHoneycomb:         return new Fill3DHoneycomb();
    case ipGyroid:              return new FillGyroid();
    case ipRectilinear:         return new FillRectilinear2();
//  case ipRectilinear:         return new FillRectilinear();
    case ipLine:                return new FillLine();
    case ipGrid:                return new FillGrid2();
    case ipTriangles:           return new FillTriangles();
    case ipStars:               return new FillStars();
    case ipCubic:               return new FillCubic();
//  case ipGrid:                return new FillGrid();
    case ipArchimedeanChords:   return new FillArchimedeanChords();
    case ipHilbertCurve:        return new FillHilbertCurve();
    case ipOctagramSpiral:      return new FillOctagramSpiral();
    default: throw std::invalid_argument("unknown type");;
    }
}

Fill* Fill::new_from_type(const std::string &type)
{
    const t_config_enum_values &enum_keys_map = ConfigOptionEnum<InfillPattern>::get_enum_values();
    t_config_enum_values::const_iterator it = enum_keys_map.find(type);
    return (it == enum_keys_map.end()) ? nullptr : new_from_type(InfillPattern(it->second));
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




/// cut poly between poly.point[idx_1] & poly.point[idx_1+1]
/// add p1+-width to one part and p2+-width to the other one.
/// add the "new" polyline to polylines (to part cut from poly)
/// p1 & p2 have to be between poly.point[idx_1] & poly.point[idx_1+1]
/// if idx_1 is ==0 or == size-1, then we don't need to create a new polyline.
void cut_polyline(Polyline &poly, Polylines &polylines, size_t idx_1, Point p1, Point p2) {
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
void cut_polygon(Polyline &poly, size_t idx_1, Point p1, Point p2) {
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
bool collision(const Points &pts_to_check, const Polylines &polylines_blocker, const coordf_t width) {
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
Points getFrontier(Polylines &polylines, const Point& p1, const Point& p2, const coord_t width, const Polylines &polylines_blockers, coord_t max_size = -1) {
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
void Fill::connect_infill(const Polylines &infill_ordered, const ExPolygon &boundary, Polylines &polylines_out, const FillParams &params) {

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
                Points pts_frontier = getFrontier(polylines_frontier, last_point, first_point, scale_(this->spacing), polylines_blocker, (coord_t)scale_(ideal_length) * 2);
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
            Points pts_frontier = getFrontier(polylines_frontier, last_point, first_point, scale_(this->spacing), polylines_blocker);
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
    for (int idx1 = 0; idx1 < polylines_connected.size(); idx1++) {
        size_t min_idx = 0;
        coordf_t min_length = 0;
        bool switch_id1 = false;
        bool switch_id2 = false;
        for (int idx2 = idx1 + 1; idx2 < polylines_connected.size(); idx2++) {
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
            Points pts_frontier = getFrontier(polylines_frontier, 
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
        Points pts_frontier = getFrontier(polylines_frontier, polyline.last_point(), polyline.first_point(), scale_(this->spacing), polylines_blocker);
        if (!pts_frontier.empty()) {
            polyline.points.insert(polyline.points.end(), pts_frontier.begin(), pts_frontier.end());
            polyline.points.insert(polyline.points.begin(), polyline.points.back());
        }
        polylines_out.emplace_back(polyline);
    }
}

} // namespace Slic3r
