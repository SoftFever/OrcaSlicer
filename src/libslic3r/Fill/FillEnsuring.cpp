///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Arachne/WallToolPaths.hpp"

#include "AABBTreeLines.hpp"
#include "Algorithm/PathSorting.hpp"
#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "FillEnsuring.hpp"
#include "KDTreeIndirect.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "SVG.hpp"
#include "libslic3r.h"

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace Slic3r {

ThickPolylines make_fill_polylines(
    const Fill *fill, const Surface *surface, const FillParams &params, bool stop_vibrations, bool fill_gaps, bool connect_extrusions)
{
    assert(fill->print_config != nullptr && fill->print_object_config != nullptr);

    auto rotate_thick_polylines = [](ThickPolylines &tpolylines, double cos_angle, double sin_angle) {
        for (ThickPolyline &tp : tpolylines) {
            for (auto &p : tp.points) {
                double px = double(p.x());
                double py = double(p.y());
                p.x()     = coord_t(round(cos_angle * px - sin_angle * py));
                p.y()     = coord_t(round(cos_angle * py + sin_angle * px));
            }
        }
    };

    auto segments_overlap = [](coord_t alow, coord_t ahigh, coord_t blow, coord_t bhigh) {
        return (alow >= blow && alow <= bhigh) || (ahigh >= blow && ahigh <= bhigh) || (blow >= alow && blow <= ahigh) ||
               (bhigh >= alow && bhigh <= ahigh);
    };

    const coord_t           scaled_spacing                      = scaled<coord_t>(fill->spacing);
    double                  distance_limit_reconnection         = 2.0 * double(scaled_spacing);
    double                  squared_distance_limit_reconnection = distance_limit_reconnection * distance_limit_reconnection;
    Polygons                filled_area                         = to_polygons(surface->expolygon);
    std::pair<float, Point> rotate_vector                       = fill->_infill_direction(surface);
    double                  aligning_angle                      = -rotate_vector.first + PI;
    polygons_rotate(filled_area, aligning_angle);
    BoundingBox bb = get_extents(filled_area);

    Polygons inner_area = stop_vibrations ? intersection(filled_area, opening(filled_area, 2 * scaled_spacing, 3 * scaled_spacing)) :
                                            filled_area;
    
    inner_area = shrink(inner_area, scaled_spacing * 0.5 - scaled<double>(fill->overlap));
    
    AABBTreeLines::LinesDistancer<Line> area_walls{to_lines(inner_area)};

    const size_t      n_vlines = (bb.max.x() - bb.min.x() + scaled_spacing - 1) / scaled_spacing;
    std::vector<Line> vertical_lines(n_vlines);
    coord_t           y_min = bb.min.y();
    coord_t           y_max = bb.max.y();
    for (size_t i = 0; i < n_vlines; i++) {
        coord_t x           = bb.min.x() + i * double(scaled_spacing);
        vertical_lines[i].a = Point{x, y_min};
        vertical_lines[i].b = Point{x, y_max};
    }
    if (vertical_lines.size() > 0) {
        vertical_lines.push_back(vertical_lines.back());
        vertical_lines.back().a = Point{coord_t(bb.min.x() + n_vlines * double(scaled_spacing) + scaled_spacing * 0.5), y_min};
        vertical_lines.back().b = Point{vertical_lines.back().a.x(), y_max};
    }

    std::vector<std::vector<Line>> polygon_sections(n_vlines);

    for (size_t i = 0; i < n_vlines; i++) {
        const auto intersections = area_walls.intersections_with_line<true>(vertical_lines[i]);

        for (int intersection_idx = 0; intersection_idx < int(intersections.size()) - 1; intersection_idx++) {
            const auto &a = intersections[intersection_idx];
            const auto &b = intersections[intersection_idx + 1];
            if (area_walls.outside((a.first + b.first) / 2) < 0) {
                if (std::abs(a.first.y() - b.first.y()) > scaled_spacing) {
                    polygon_sections[i].emplace_back(a.first, b.first);
                }
            }
        }
    }

    if (stop_vibrations) {
        struct Node
        {
            int                              section_idx;
            int                              line_idx;
            int                              skips_taken         = 0;
            bool                             neighbours_explored = false;
            std::vector<std::pair<int, int>> neighbours{};
        };

        coord_t length_filter     = scale_(4);
        size_t  skips_allowed     = 2;
        size_t  min_removal_conut = 5;
        for (int section_idx = 0; section_idx < int(polygon_sections.size()); ++ section_idx) {
            for (int line_idx = 0; line_idx < int(polygon_sections[section_idx].size()); ++ line_idx) {
                if (const Line &line = polygon_sections[section_idx][line_idx]; line.a != line.b && line.length() < length_filter) {
                    std::set<std::pair<int, int>> to_remove{{section_idx, line_idx}};
                    std::vector<Node>             to_visit{{section_idx, line_idx}};

                    bool initial_touches_long_lines = false;
                    if (section_idx > 0) {
                        for (int prev_line_idx = 0; prev_line_idx < int(polygon_sections[section_idx - 1].size()); ++ prev_line_idx) {
                            if (const Line &nl = polygon_sections[section_idx - 1][prev_line_idx];
                                nl.a != nl.b && segments_overlap(line.a.y(), line.b.y(), nl.a.y(), nl.b.y())) {
                                initial_touches_long_lines = true;
                            }
                        }
                    }

                    while (!to_visit.empty()) {
                        Node        curr   = to_visit.back();
                        const Line &curr_l = polygon_sections[curr.section_idx][curr.line_idx];
                        if (curr.neighbours_explored) {
                            bool is_valid_for_removal = (curr_l.length() < length_filter) &&
                                                        ((int(to_remove.size()) - curr.skips_taken > int(min_removal_conut)) ||
                                                         (curr.neighbours.empty() && !initial_touches_long_lines));
                            if (!is_valid_for_removal) {
                                for (const auto &n : curr.neighbours) {
                                    if (to_remove.find(n) != to_remove.end()) {
                                        is_valid_for_removal = true;
                                        break;
                                    }
                                }
                            }
                            if (!is_valid_for_removal) {
                                to_remove.erase({curr.section_idx, curr.line_idx});
                            }
                            to_visit.pop_back();
                        } else {
                            to_visit.back().neighbours_explored = true;
                            int  curr_index                     = to_visit.size() - 1;
                            bool can_use_skip                   = curr_l.length() <= length_filter && curr.skips_taken < int(skips_allowed);
                            if (curr.section_idx + 1 < int(polygon_sections.size())) {
                                for (int lidx = 0; lidx < int(polygon_sections[curr.section_idx + 1].size()); ++ lidx) {
                                    if (const Line &nl = polygon_sections[curr.section_idx + 1][lidx];
                                        nl.a != nl.b && segments_overlap(curr_l.a.y(), curr_l.b.y(), nl.a.y(), nl.b.y()) &&
                                        (nl.length() < length_filter || can_use_skip)) {
                                        to_visit[curr_index].neighbours.push_back({curr.section_idx + 1, lidx});
                                        to_remove.insert({curr.section_idx + 1, lidx});
                                        Node next_node{curr.section_idx + 1, lidx, curr.skips_taken + (nl.length() >= length_filter)};
                                        to_visit.push_back(next_node);
                                    }
                                }
                            }
                        }
                    }

                    for (const auto &pair : to_remove) {
                        Line &l = polygon_sections[pair.first][pair.second];
                        l.a     = l.b;
                    }
                }
            }
        }
    }

    for (size_t section_idx = 0; section_idx < polygon_sections.size(); section_idx++) {
        polygon_sections[section_idx].erase(std::remove_if(polygon_sections[section_idx].begin(), polygon_sections[section_idx].end(),
                                                           [](const Line &s) { return s.a == s.b; }),
                                            polygon_sections[section_idx].end());
        std::sort(polygon_sections[section_idx].begin(), polygon_sections[section_idx].end(),
                  [](const Line &a, const Line &b) { return a.a.y() < b.b.y(); });
    }

    ThickPolylines thick_polylines;
    {
        for (const auto &polygon_slice : polygon_sections) {
            for (const Line &segment : polygon_slice) {
                ThickPolyline &new_path = thick_polylines.emplace_back();
                new_path.points.push_back(segment.a);
                new_path.width.push_back(scaled_spacing);
                new_path.points.push_back(segment.b);
                new_path.width.push_back(scaled_spacing);
                new_path.endpoints = {true, true};
            }
        }
    }

    if (fill_gaps) {
        Polygons reconstructed_area{};
        // reconstruct polygon from polygon sections
        {
            struct TracedPoly
            {
                Points lows;
                Points highs;
            };

            std::vector<std::vector<Line>> polygon_sections_w_width = polygon_sections;
            for (auto &slice : polygon_sections_w_width) {
                for (Line &l : slice) {
                    l.a -= Point{0.0, 0.5 * scaled_spacing};
                    l.b += Point{0.0, 0.5 * scaled_spacing};
                }
            }

            std::vector<TracedPoly> current_traced_polys;
            for (const auto &polygon_slice : polygon_sections_w_width) {
                std::unordered_set<const Line *> used_segments;
                for (TracedPoly &traced_poly : current_traced_polys) {
                    auto candidates_begin = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.lows.back(),
                                                             [](const Point &low, const Line &seg) { return seg.b.y() > low.y(); });
                    auto candidates_end   = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.highs.back(),
                                                             [](const Point &high, const Line &seg) { return seg.a.y() > high.y(); });

                    bool segment_added = false;
                    for (auto candidate = candidates_begin; candidate != candidates_end && !segment_added; candidate++) {
                        if (used_segments.find(&(*candidate)) != used_segments.end()) {
                            continue;
                        }
                        if (connect_extrusions && (traced_poly.lows.back() - candidates_begin->a).cast<double>().squaredNorm() <
                                                      squared_distance_limit_reconnection) {
                            traced_poly.lows.push_back(candidates_begin->a);
                        } else {
                            traced_poly.lows.push_back(traced_poly.lows.back() + Point{scaled_spacing / 2, 0});
                            traced_poly.lows.push_back(candidates_begin->a - Point{scaled_spacing / 2, 0});
                            traced_poly.lows.push_back(candidates_begin->a);
                        }

                        if (connect_extrusions && (traced_poly.highs.back() - candidates_begin->b).cast<double>().squaredNorm() <
                                                      squared_distance_limit_reconnection) {
                            traced_poly.highs.push_back(candidates_begin->b);
                        } else {
                            traced_poly.highs.push_back(traced_poly.highs.back() + Point{scaled_spacing / 2, 0});
                            traced_poly.highs.push_back(candidates_begin->b - Point{scaled_spacing / 2, 0});
                            traced_poly.highs.push_back(candidates_begin->b);
                        }
                        segment_added = true;
                        used_segments.insert(&(*candidates_begin));
                    }

                    if (!segment_added) {
                        // Zero or multiple overlapping segments. Resolving this is nontrivial,
                        // so we just close this polygon and maybe open several new. This will hopefully happen much less often
                        traced_poly.lows.push_back(traced_poly.lows.back() + Point{scaled_spacing / 2, 0});
                        traced_poly.highs.push_back(traced_poly.highs.back() + Point{scaled_spacing / 2, 0});
                        Polygon &new_poly = reconstructed_area.emplace_back(std::move(traced_poly.lows));
                        new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
                        traced_poly.lows.clear();
                        traced_poly.highs.clear();
                    }
                }

                current_traced_polys.erase(std::remove_if(current_traced_polys.begin(), current_traced_polys.end(),
                                                          [](const TracedPoly &tp) { return tp.lows.empty(); }),
                                           current_traced_polys.end());

                for (const auto &segment : polygon_slice) {
                    if (used_segments.find(&segment) == used_segments.end()) {
                        TracedPoly &new_tp = current_traced_polys.emplace_back();
                        new_tp.lows.push_back(segment.a - Point{scaled_spacing / 2, 0});
                        new_tp.lows.push_back(segment.a);
                        new_tp.highs.push_back(segment.b - Point{scaled_spacing / 2, 0});
                        new_tp.highs.push_back(segment.b);
                    }
                }
            }

            // add not closed polys
            for (TracedPoly &traced_poly : current_traced_polys) {
                Polygon &new_poly = reconstructed_area.emplace_back(std::move(traced_poly.lows));
                new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
            }
        }

        reconstructed_area                     = union_safety_offset(reconstructed_area);
        ExPolygons gaps_for_additional_filling = diff_ex(filled_area, reconstructed_area);
        if (fill->overlap != 0) {
            gaps_for_additional_filling = offset_ex(gaps_for_additional_filling, scaled<float>(fill->overlap));
        }

        // BoundingBox bbox = get_extents(filled_area);
        // bbox.offset(scale_(1.));
        // ::Slic3r::SVG svg(debug_out_path(("surface" + std::to_string(surface->area())).c_str()).c_str(), bbox);
        // svg.draw(to_lines(filled_area), "red", scale_(0.4));
        // svg.draw(to_lines(reconstructed_area), "blue", scale_(0.3));
        // svg.draw(to_lines(gaps_for_additional_filling), "green", scale_(0.2));
        // svg.draw(vertical_lines, "black", scale_(0.1));
        // svg.Close();

        Arachne::WallToolPathsParams input_params = Arachne::make_paths_params(fill->layer_id, *fill->print_object_config, *fill->print_config);
        for (ExPolygon &ex_poly : gaps_for_additional_filling) {
            BoundingBox            ex_bb       = ex_poly.contour.bounding_box();
            coord_t                loops_count = (std::max(ex_bb.size().x(), ex_bb.size().y()) + scaled_spacing - 1) / scaled_spacing;
            Polygons               polygons    = to_polygons(ex_poly);
            Arachne::WallToolPaths wall_tool_paths(polygons, scaled_spacing, scaled_spacing, loops_count, 0, params.layer_height,
                                                   input_params);
            if (std::vector<Arachne::VariableWidthLines> loops = wall_tool_paths.getToolPaths(); !loops.empty()) {
                std::vector<const Arachne::ExtrusionLine *> all_extrusions;
                for (Arachne::VariableWidthLines &loop : loops) {
                    if (loop.empty())
                        continue;
                    for (const Arachne::ExtrusionLine &wall : loop)
                        all_extrusions.emplace_back(&wall);
                }

                for (const Arachne::ExtrusionLine *extrusion : all_extrusions) {
                    if (extrusion->junctions.size() < 2) {
                        continue;
                    }
                    ThickPolyline thick_polyline = Arachne::to_thick_polyline(*extrusion);
                    if (extrusion->is_closed) {
                        thick_polyline.start_at_index(ex_bb.min.nearest_point_index(thick_polyline.points));
                        thick_polyline.clip_end(scaled_spacing * 0.5);
                    }
                    if (thick_polyline.is_valid() && thick_polyline.length() > 0 && thick_polyline.points.size() > 1) {
                        thick_polylines.push_back(thick_polyline);
                    }
                }
            }
        }

        std::sort(thick_polylines.begin(), thick_polylines.end(), [](const ThickPolyline &left, const ThickPolyline &right) {
            BoundingBox lbb(left.points);
            BoundingBox rbb(right.points);
            if (lbb.min.x() == rbb.min.x())
                return lbb.min.y() < rbb.min.y();
            else
                return lbb.min.x() < rbb.min.x();
        });

        // connect tiny gap fills to close colinear line
        struct EndPoint
        {
            Vec2d  position;
            size_t polyline_idx;
            size_t other_end_point_idx;
            bool   is_first;
            bool   used = false;
        };
        std::vector<EndPoint> connection_endpoints;
        connection_endpoints.reserve(thick_polylines.size() * 2);
        for (size_t pl_idx = 0; pl_idx < thick_polylines.size(); pl_idx++) {
            size_t current_idx = connection_endpoints.size();
            connection_endpoints.push_back({thick_polylines[pl_idx].first_point().cast<double>(), pl_idx, current_idx + 1, true});
            connection_endpoints.push_back({thick_polylines[pl_idx].last_point().cast<double>(), pl_idx, current_idx, false});
        }

        std::vector<bool> linear_segment_flags(thick_polylines.size());
        for (size_t i = 0;i < thick_polylines.size(); i++) {
            const ThickPolyline& tp = thick_polylines[i];
            linear_segment_flags[i] = tp.points.size() == 2 && tp.points.front().x() == tp.points.back().x() &&
                                      tp.width.front() == scaled_spacing && tp.width.back() == scaled_spacing;
        }

        auto coord_fn = [&connection_endpoints](size_t idx, size_t dim) { return connection_endpoints[idx].position[dim]; };
        KDTreeIndirect<2, double, decltype(coord_fn)> endpoints_tree{coord_fn, connection_endpoints.size()};
        for (size_t ep_idx = 0; ep_idx < connection_endpoints.size(); ep_idx++) {
            EndPoint &ep1 = connection_endpoints[ep_idx];
            if (!ep1.used) {
                std::vector<size_t> close_endpoints = find_nearby_points(endpoints_tree, ep1.position, double(scaled_spacing));
                for (size_t close_endpoint_idx : close_endpoints) {
                    EndPoint &ep2 = connection_endpoints[close_endpoint_idx];
                    if (ep2.used || ep2.polyline_idx == ep1.polyline_idx ||
                        (linear_segment_flags[ep1.polyline_idx] && linear_segment_flags[ep2.polyline_idx])) {
                        continue;
                    }

                    EndPoint &target_ep = ep1.polyline_idx > ep2.polyline_idx ? ep1 : ep2;
                    EndPoint &source_ep = ep1.polyline_idx > ep2.polyline_idx ? ep2 : ep1;

                    ThickPolyline &target_tp                     = thick_polylines[target_ep.polyline_idx];
                    ThickPolyline &source_tp                     = thick_polylines[source_ep.polyline_idx];
                    linear_segment_flags[target_ep.polyline_idx] = linear_segment_flags[ep1.polyline_idx] ||
                                                                   linear_segment_flags[ep2.polyline_idx];

                    Vec2d v1 = target_ep.is_first ?
                                   (target_tp.points[0] - target_tp.points[1]).cast<double>() :
                                   (target_tp.points.back() - target_tp.points[target_tp.points.size() - 1]).cast<double>();
                    Vec2d v2 = source_ep.is_first ?
                                   (source_tp.points[1] - source_tp.points[0]).cast<double>() :
                                   (source_tp.points[source_tp.points.size() - 1] - source_tp.points.back()).cast<double>();

                    if (std::abs(Slic3r::angle(v1, v2)) > PI / 6.0) {
                        continue;
                    }

                    // connect target_ep and source_ep, result is stored in target_tp, source_tp will be cleared
                    if (target_ep.is_first) {
                        target_tp.reverse();
                        target_ep.is_first                                           = false;
                        connection_endpoints[target_ep.other_end_point_idx].is_first = true;
                    }

                    size_t new_start_idx = target_ep.other_end_point_idx;

                    if (!source_ep.is_first) {
                        source_tp.reverse();
                        source_ep.is_first                                           = true;
                        connection_endpoints[source_ep.other_end_point_idx].is_first = false;
                    }

                    size_t new_end_idx = source_ep.other_end_point_idx;

                    target_tp.points.insert(target_tp.points.end(), source_tp.points.begin(), source_tp.points.end());
                    target_tp.width.push_back(target_tp.width.back());
                    target_tp.width.push_back(source_tp.width.front());
                    target_tp.width.insert(target_tp.width.end(), source_tp.width.begin(), source_tp.width.end());
                    target_ep.used = true;
                    source_ep.used = true;

                    connection_endpoints[new_start_idx].polyline_idx        = target_ep.polyline_idx;
                    connection_endpoints[new_end_idx].polyline_idx          = target_ep.polyline_idx;
                    connection_endpoints[new_start_idx].other_end_point_idx = new_end_idx;
                    connection_endpoints[new_end_idx].other_end_point_idx   = new_start_idx;
                    source_tp.clear();
                    break;
                }
            }
        }

        thick_polylines.erase(std::remove_if(thick_polylines.begin(), thick_polylines.end(),
                                             [scaled_spacing](const ThickPolyline &tp) {
                                                 return tp.length() < scaled_spacing &&
                                                        std::all_of(tp.width.begin(), tp.width.end(),
                                                                    [scaled_spacing](double w) { return w < scaled_spacing; });
                                             }),
                              thick_polylines.end());
    }

    Algorithm::sort_paths(thick_polylines.begin(), thick_polylines.end(), bb.min, double(scaled_spacing) * 1.2, [](const ThickPolyline &tp) {
        Lines ls;
        Point prev = tp.first_point();
        for (size_t i = 1; i < tp.points.size(); i++) {
            ls.emplace_back(prev, tp.points[i]);
            prev = ls.back().b;
        }
        return ls;
    });

    if (connect_extrusions) {
        ThickPolylines connected_thick_polylines;
        if (!thick_polylines.empty()) {
            connected_thick_polylines.push_back(thick_polylines.front());
            for (size_t tp_idx = 1; tp_idx < thick_polylines.size(); tp_idx++) {
                ThickPolyline &tp   = thick_polylines[tp_idx];
                ThickPolyline &tail = connected_thick_polylines.back();
                Point          last = tail.last_point();
                if ((last - tp.last_point()).cast<double>().squaredNorm() < (last - tp.first_point()).cast<double>().squaredNorm()) {
                    tp.reverse();
                }
                if ((last - tp.first_point()).cast<double>().squaredNorm() < squared_distance_limit_reconnection) {
                    tail.points.insert(tail.points.end(), tp.points.begin(), tp.points.end());
                    tail.width.push_back(scaled_spacing);
                    tail.width.push_back(scaled_spacing);
                    tail.width.insert(tail.width.end(), tp.width.begin(), tp.width.end());
                } else {
                    connected_thick_polylines.push_back(tp);
                }
            }
        }
        thick_polylines = connected_thick_polylines;
    }

    rotate_thick_polylines(thick_polylines, cos(-aligning_angle), sin(-aligning_angle));
    return thick_polylines;
}

} // namespace Slic3r
