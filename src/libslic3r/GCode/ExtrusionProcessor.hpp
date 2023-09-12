#ifndef slic3r_ExtrusionProcessor_hpp_
#define slic3r_ExtrusionProcessor_hpp_

// This algorithm is copied from PrusaSlicer, original author is Pavel Mikus(pavel.mikus.mail@seznam.cz)

#include "../AABBTreeLines.hpp"
//#include "../SupportSpotsGenerator.hpp"
#include "../libslic3r.h"
#include "../ExtrusionEntity.hpp"
#include "../Layer.hpp"
#include "../Point.hpp"
#include "../SVG.hpp"
#include "../BoundingBox.hpp"
#include "../Polygon.hpp"
#include "../ClipperUtils.hpp"
#include "../Flow.hpp"
#include "../Config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r {

class SlidingWindowCurvatureAccumulator
{
    float        window_size;
    float        total_distance  = 0; // accumulated distance
    float        total_curvature = 0; // accumulated signed ccw angles
    deque<float> distances;
    deque<float> angles;

public:
    SlidingWindowCurvatureAccumulator(float window_size) : window_size(window_size) {}

    void add_point(float distance, float angle)
    {
        total_distance += distance;
        total_curvature += angle;
        distances.push_back(distance);
        angles.push_back(angle);

        while (distances.size() > 1 && total_distance > window_size) {
            total_distance -= distances.front();
            total_curvature -= angles.front();
            distances.pop_front();
            angles.pop_front();
        }
    }

    float get_curvature() const
    {
        return total_curvature / window_size;
    }

    void reset()
    {
        total_curvature = 0;
        total_distance  = 0;
        distances.clear();
        angles.clear();
    }
};

class CurvatureEstimator
{
    static const size_t               sliders_count          = 3;
    SlidingWindowCurvatureAccumulator sliders[sliders_count] = {{3.0},{9.0}, {16.0}};

public:
    void add_point(float distance, float angle)
    {
        if (distance < EPSILON)
            return;
        for (SlidingWindowCurvatureAccumulator &slider : sliders) {
            slider.add_point(distance, angle);
        }
    }
    float get_curvature()
    {
        float max_curvature = 0.0f;
        for (const SlidingWindowCurvatureAccumulator &slider : sliders) {
            if (abs(slider.get_curvature()) > abs(max_curvature)) {
                max_curvature = slider.get_curvature();
            }
        }
        return max_curvature;
    }
    void reset()
    {
        for (SlidingWindowCurvatureAccumulator &slider : sliders) {
            slider.reset();
        }
    }
};

struct ExtendedPoint
{
    ExtendedPoint(Vec2d position, float distance = 0.0, size_t nearest_prev_layer_line = size_t(-1), float curvature = 0.0)
        : position(position), distance(distance), nearest_prev_layer_line(nearest_prev_layer_line), curvature(curvature)
    {}

    Vec2d  position;
    float  distance;
    size_t nearest_prev_layer_line;
    float  curvature;
};

template<bool SCALED_INPUT, bool ADD_INTERSECTIONS, bool PREV_LAYER_BOUNDARY_OFFSET, bool SIGNED_DISTANCE, typename P, typename L>
std::vector<ExtendedPoint> estimate_points_properties(const std::vector<P>                   &input_points,
                                                      const AABBTreeLines::LinesDistancer<L> &unscaled_prev_layer,
                                                      float                                   flow_width,
                                                      float                                   max_line_length = -1.0f)
{
    using AABBScalar = typename AABBTreeLines::LinesDistancer<L>::Scalar;
    if (input_points.empty())
        return {};
    float              boundary_offset = PREV_LAYER_BOUNDARY_OFFSET ? 0.5 * flow_width : 0.0f;
    CurvatureEstimator cestim;
    auto maybe_unscale = [](const P &p) { return SCALED_INPUT ? unscaled(p) : p.template cast<double>(); };

    std::vector<ExtendedPoint> points;
    points.reserve(input_points.size() * (ADD_INTERSECTIONS ? 1.5 : 1));

    {
        ExtendedPoint start_point{maybe_unscale(input_points.front())};
        auto [distance, nearest_line, x]    = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(start_point.position.cast<AABBScalar>());
        start_point.distance                = distance + boundary_offset;
        start_point.nearest_prev_layer_line = nearest_line;
        points.push_back(start_point);
    }
    for (size_t i = 1; i < input_points.size(); i++) {
        ExtendedPoint next_point{maybe_unscale(input_points[i])};
        auto [distance, nearest_line, x]   = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(next_point.position.cast<AABBScalar>());
        next_point.distance                = distance + boundary_offset;
        next_point.nearest_prev_layer_line = nearest_line;

        if (ADD_INTERSECTIONS &&
            ((points.back().distance > boundary_offset + EPSILON) != (next_point.distance > boundary_offset + EPSILON))) {
            const ExtendedPoint &prev_point = points.back();
            auto intersections = unscaled_prev_layer.template intersections_with_line<true>(L{prev_point.position.cast<AABBScalar>(), next_point.position.cast<AABBScalar>()});
            for (const auto &intersection : intersections) {
                points.emplace_back(intersection.first.template cast<double>(), boundary_offset, intersection.second);
            }
        }
        points.push_back(next_point);
    }

    if (PREV_LAYER_BOUNDARY_OFFSET && ADD_INTERSECTIONS) {
        std::vector<ExtendedPoint> new_points;
        new_points.reserve(points.size()*2);
        new_points.push_back(points.front());
        for (int point_idx = 0; point_idx < int(points.size()) - 1; ++point_idx) {
            const ExtendedPoint &curr = points[point_idx];
            const ExtendedPoint &next = points[point_idx + 1];

            if ((curr.distance > 0 && curr.distance < boundary_offset + 2.0f) ||
                (next.distance > 0 && next.distance < boundary_offset + 2.0f)) {
                double line_len = (next.position - curr.position).norm();
                if (line_len > 4.0f) {
                    double a0 = std::clamp((curr.distance + 2 * boundary_offset) / line_len, 0.0, 1.0);
                    double a1 = std::clamp(1.0f - (next.distance + 2 * boundary_offset) / line_len, 0.0, 1.0);
                    double t0 = std::min(a0, a1);
                    double t1 = std::max(a0, a1);

                    if (t0 < 1.0) {
                        auto p0                         = curr.position + t0 * (next.position - curr.position);
                        auto [p0_dist, p0_near_l, p0_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(p0.cast<AABBScalar>());
                        new_points.push_back(ExtendedPoint{p0, float(p0_dist + boundary_offset), p0_near_l});
                    }
                    if (t1 > 0.0) {
                        auto p1                         = curr.position + t1 * (next.position - curr.position);
                        auto [p1_dist, p1_near_l, p1_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(p1.cast<AABBScalar>());
                        new_points.push_back(ExtendedPoint{p1, float(p1_dist + boundary_offset), p1_near_l});
                    }
                }
            }
            new_points.push_back(next);
        }
        points = new_points;
    }

    if (max_line_length > 0) {
        std::vector<ExtendedPoint> new_points;
        new_points.reserve(points.size()*2);
        {
            for (size_t i = 0; i + 1 < points.size(); i++) {
                const ExtendedPoint &curr = points[i];
                const ExtendedPoint &next = points[i + 1];
                new_points.push_back(curr);
                double len             = (next.position - curr.position).squaredNorm();
                double t               = sqrt((max_line_length * max_line_length) / len);
                size_t new_point_count = 1.0 / t;
                for (size_t j = 1; j < new_point_count + 1; j++) {
                    Vec2d pos  = curr.position * (1.0 - j * t) + next.position * (j * t);
                    auto [p_dist, p_near_l,
                          p_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(pos.cast<AABBScalar>());
                    new_points.push_back(ExtendedPoint{pos, float(p_dist + boundary_offset), p_near_l});
                }
            }
            new_points.push_back(points.back());
        }
        points = new_points;
    }

    for (int point_idx = 0; point_idx < int(points.size()); ++point_idx) {
        ExtendedPoint &a    = points[point_idx];
        ExtendedPoint &prev = points[point_idx > 0 ? point_idx - 1 : point_idx];

        int prev_point_idx = point_idx;
        while (prev_point_idx > 0) {
            prev_point_idx--;
            if ((a.position - points[prev_point_idx].position).squaredNorm() > EPSILON) { break; }
        }

        int next_point_index = point_idx;
        while (next_point_index < int(points.size()) - 1) {
            next_point_index++;
            if ((a.position - points[next_point_index].position).squaredNorm() > EPSILON) { break; }
        }

        if (prev_point_idx != point_idx && next_point_index != point_idx) {
            float distance = (prev.position - a.position).norm();
            float alfa     = angle(a.position - points[prev_point_idx].position, points[next_point_index].position - a.position);
            cestim.add_point(distance, alfa);
        }

        a.curvature = cestim.get_curvature();
    }

    return points;
}

struct ProcessedPoint
{
    Point p;
    float speed = 1.0f;
    float overlap = 1.0f;
};

class ExtrusionQualityEstimator
{
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<Linef>> prev_layer_boundaries;
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<Linef>> next_layer_boundaries;
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<CurledLine>> prev_curled_extrusions;
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<CurledLine>> next_curled_extrusions;
    const PrintObject                                                            *current_object;

public:
    void set_current_object(const PrintObject *object) { current_object = object; }

    void prepare_for_new_layer(const PrintObject * obj, const Layer *layer)
    {
        if (layer == nullptr) return;
        const PrintObject *object = obj;
        prev_layer_boundaries[object] = next_layer_boundaries[object];
        next_layer_boundaries[object] = AABBTreeLines::LinesDistancer<Linef>{to_unscaled_linesf(layer->lslices)};
        prev_curled_extrusions[object] = next_curled_extrusions[object];
        next_curled_extrusions[object] = AABBTreeLines::LinesDistancer<CurledLine>{layer->curled_lines};
    }

    std::vector<ProcessedPoint> estimate_extrusion_quality(const ExtrusionPath                &path,
                                                           const ConfigOptionPercents         &overlaps,
                                                           const ConfigOptionFloatsOrPercents &speeds,
                                                           float                               ext_perimeter_speed,
                                                           float                               original_speed,
                                                           bool								   slowdown_for_curled_edges)
    {
        size_t                               speed_sections_count = std::min(overlaps.values.size(), speeds.values.size());
        std::vector<std::pair<float, float>> speed_sections;
        
        
        
        for (size_t i = 0; i < speed_sections_count; i++) {
            float distance = path.width * (1.0 - (overlaps.get_at(i) / 100.0));
            float speed    = speeds.get_at(i).percent ? (ext_perimeter_speed * speeds.get_at(i).value / 100.0) : speeds.get_at(i).value;
            speed_sections.push_back({distance, speed});
        }
        std::sort(speed_sections.begin(), speed_sections.end(),
                  [](const std::pair<float, float> &a, const std::pair<float, float> &b) { 
                    if (a.first == b.first) {
                        return a.second > b.second;
                    }
                    return a.first < b.first; });

        std::pair<float, float> last_section{INFINITY, 0};
        for (auto &section : speed_sections) {
            if (section.first == last_section.first) {
                section.second = last_section.second;
            } else {
                last_section = section;
            }
        }

        std::vector<ExtendedPoint> extended_points =
            estimate_points_properties<true, true, true, true>(path.polyline.points, prev_layer_boundaries[current_object], path.width);
        const auto width_inv = 1.0f / path.width;
        std::vector<ProcessedPoint> processed_points;
        processed_points.reserve(extended_points.size());
        for (size_t i = 0; i < extended_points.size(); i++) {
            const ExtendedPoint &curr = extended_points[i];
            const ExtendedPoint &next = extended_points[i + 1 < extended_points.size() ? i + 1 : i];
            
            float artificial_distance_to_curled_lines = 0.0;
            if(slowdown_for_curled_edges) {
            	// The following code artifically increases the distance to provide slowdown for extrusions that are over curled lines
            	const double dist_limit = 10.0 * path.width;
				{
				Vec2d middle = 0.5 * (curr.position + next.position);
				auto line_indices = prev_curled_extrusions[current_object].all_lines_in_radius(Point::new_scale(middle), scale_(dist_limit));
					if (!line_indices.empty()) {
						double len   = (next.position - curr.position).norm();
						// For long lines, there is a problem with the additional slowdown. If by accident, there is small curled line near the middle of this long line
                    	//  The whole segment gets slower unnecesarily. For these long lines, we do additional check whether it is worth slowing down.
                    	// NOTE that this is still quite rough approximation, e.g. we are still checking lines only near the middle point
                    	// TODO maybe split the lines into smaller segments before running this alg? but can be demanding, and GCode will be huge
                    	if (len > 8) {
                        	Vec2d dir   = Vec2d(next.position - curr.position) / len;
                        	Vec2d right = Vec2d(-dir.y(), dir.x());

                        	Polygon box_of_influence = {
                            	scaled(Vec2d(curr.position + right * dist_limit)),
                            	scaled(Vec2d(next.position + right * dist_limit)),
                            	scaled(Vec2d(next.position - right * dist_limit)),
                            	scaled(Vec2d(curr.position - right * dist_limit)),
                        	};

                        	double projected_lengths_sum = 0;
                        	for (size_t idx : line_indices) {
                            	const CurledLine &line   = prev_curled_extrusions[current_object].get_line(idx);
                            	Lines             inside = intersection_ln({{line.a, line.b}}, {box_of_influence});
                            	if (inside.empty())
                                	continue;
                            	double projected_length = abs(dir.dot(unscaled(Vec2d((inside.back().b - inside.back().a).cast<double>()))));
                            	projected_lengths_sum += projected_length;
                        	}
                        	if (projected_lengths_sum < 0.4 * len) {
                            	line_indices.clear();
                        	}
                    	}
                    
                    	for (size_t idx : line_indices) {
                        	const CurledLine &line                 = prev_curled_extrusions[current_object].get_line(idx);
                        	float             distance_from_curled = unscaled(line_alg::distance_to(line, Point::new_scale(middle)));
                        	float             dist                 = path.width * (1.0 - (distance_from_curled / dist_limit)) *
                                     (1.0 - (distance_from_curled / dist_limit)) *
                                     (line.curled_height / (path.height * 10.0f)); // max_curled_height_factor from SupportSpotGenerator
                        	artificial_distance_to_curled_lines = std::max(artificial_distance_to_curled_lines, dist);
                    	}
					}
				}
			}	

            auto calculate_speed = [&speed_sections, &original_speed](float distance) {
                float final_speed;
                if (distance <= speed_sections.front().first) {
                    final_speed = original_speed;
                } else if (distance >= speed_sections.back().first) {
                    final_speed = speed_sections.back().second;
                } else {
                    size_t section_idx = 0;
                    while (distance > speed_sections[section_idx + 1].first) {
                        section_idx++;
                    }
                    float t = (distance - speed_sections[section_idx].first) /
                              (speed_sections[section_idx + 1].first - speed_sections[section_idx].first);
                    t           = std::clamp(t, 0.0f, 1.0f);
                    final_speed = (1.0f - t) * speed_sections[section_idx].second + t * speed_sections[section_idx + 1].second;
                }
                return final_speed;
            };
            
            float extrusion_speed = std::min(calculate_speed(curr.distance), calculate_speed(next.distance));
            if(slowdown_for_curled_edges) {
            	float curled_speed = calculate_speed(artificial_distance_to_curled_lines);
            	extrusion_speed       = std::min(curled_speed, extrusion_speed); // adjust extrusion speed based on what is smallest - the calculated overhang speed or the artificial curled speed
            }
            
            float overlap = std::min(1 - curr.distance * width_inv, 1 - next.distance * width_inv);
			
            processed_points.push_back({ scaled(curr.position), extrusion_speed, overlap });
        }
        return processed_points;
    }
};

} // namespace Slic3r

#endif // slic3r_ExtrusionProcessor_hpp_
