#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "EdgeGrid.hpp"
#include "Layer.hpp"
#include "Print.hpp"
#include "Geometry/VoronoiVisualUtils.hpp"
#include "Geometry/VoronoiUtils.hpp"
#include "MutablePolygon.hpp"
#include "format.hpp"

#include <utility>
#include <unordered_set>

#include <boost/log/trivial.hpp>
#include <tbb/parallel_for.h>
#include <mutex>
#include <boost/thread/lock_guard.hpp>

//#define MM_SEGMENTATION_DEBUG_GRAPH
//#define MM_SEGMENTATION_DEBUG_REGIONS
//#define MM_SEGMENTATION_DEBUG_INPUT
//#define MM_SEGMENTATION_DEBUG_PAINTED_LINES
//#define MM_SEGMENTATION_DEBUG_COLORIZED_POLYGONS

#if defined(MM_SEGMENTATION_DEBUG_GRAPH) || defined(MM_SEGMENTATION_DEBUG_REGIONS) || \
    defined(MM_SEGMENTATION_DEBUG_INPUT) || defined(MM_SEGMENTATION_DEBUG_PAINTED_LINES) || \
    defined(MM_SEGMENTATION_DEBUG_COLORIZED_POLYGONS)
#define MM_SEGMENTATION_DEBUG
#endif

//#define MM_SEGMENTATION_DEBUG_TOP_BOTTOM

namespace Slic3r {
bool is_equal(float left, float right, float eps = 1e-3) {
    return abs(left - right) <= eps;
}

bool is_less(float left, float right, float eps = 1e-3) {
    return left + eps < right;
}

// Assumes that is at most same projected_l length or below than projection_l
static bool project_line_on_line(const Line &projection_l, const Line &projected_l, Line *new_projected)
{
    const Vec2d  v1 = (projection_l.b - projection_l.a).cast<double>();
    const Vec2d  va = (projected_l.a - projection_l.a).cast<double>();
    const Vec2d  vb = (projected_l.b - projection_l.a).cast<double>();
    const double l2 = v1.squaredNorm(); // avoid a sqrt
    if (l2 == 0.0)
        return false;
    double t1 = va.dot(v1) / l2;
    double t2 = vb.dot(v1) / l2;
    t1        = std::clamp(t1, 0., 1.);
    t2        = std::clamp(t2, 0., 1.);
    assert(t1 >= 0.);
    assert(t2 >= 0.);
    assert(t1 <= 1.);
    assert(t2 <= 1.);

    Point p1       = projection_l.a + (t1 * v1).cast<coord_t>();
    Point p2       = projection_l.a + (t2 * v1).cast<coord_t>();
    *new_projected = Line(p1, p2);
    return true;
}

struct PaintedLine
{
    size_t contour_idx;
    size_t line_idx;
    Line   projected_line;
    int    color;
};

struct PaintedLineVisitor
{
    PaintedLineVisitor(const EdgeGrid::Grid &grid, std::vector<PaintedLine> &painted_lines, std::mutex &painted_lines_mutex, size_t reserve) : grid(grid), painted_lines(painted_lines), painted_lines_mutex(painted_lines_mutex)
    {
        painted_lines_set.reserve(reserve);
    }

    void reset() { painted_lines_set.clear(); }

    bool operator()(coord_t iy, coord_t ix)
    {
        // Called with a row and column of the grid cell, which is intersected by a line.
        auto         cell_data_range        = grid.cell_data_range(iy, ix);
        const Vec2d  v1                     = line_to_test.vector().cast<double>();
        const double v1_sqr_norm            = v1.squaredNorm();
        const double heuristic_thr_part     = line_to_test.length() + append_threshold;
        for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {
            Line        grid_line         = grid.line(*it_contour_and_segment);
            const Vec2d v2                = grid_line.vector().cast<double>();
            double      heuristic_thr_sqr = Slic3r::sqr(heuristic_thr_part + grid_line.length());

            // An inexpensive heuristic to test whether line_to_test and grid_line can be somewhere close enough to each other.
            // This helps filter out cases when the following expensive calculations are useless.
            if ((grid_line.a - line_to_test.a).cast<double>().squaredNorm() > heuristic_thr_sqr ||
                (grid_line.b - line_to_test.a).cast<double>().squaredNorm() > heuristic_thr_sqr ||
                (grid_line.a - line_to_test.b).cast<double>().squaredNorm() > heuristic_thr_sqr ||
                (grid_line.b - line_to_test.b).cast<double>().squaredNorm() > heuristic_thr_sqr)
                continue;

            // When lines have too different length, it is necessary to normalize them
            if (Slic3r::sqr(v1.dot(v2)) > cos_threshold2 * v1_sqr_norm * v2.squaredNorm()) {
                // The two vectors are nearly collinear (their mutual angle is lower than 30 degrees)
                if (painted_lines_set.find(*it_contour_and_segment) == painted_lines_set.end()) {
                    if (grid_line.distance_to_squared(line_to_test.a) < append_threshold2 ||
                        grid_line.distance_to_squared(line_to_test.b) < append_threshold2 ||
                        line_to_test.distance_to_squared(grid_line.a) < append_threshold2 ||
                        line_to_test.distance_to_squared(grid_line.b) < append_threshold2) {
                        Line line_to_test_projected;
                        project_line_on_line(grid_line, line_to_test, &line_to_test_projected);

                        if ((line_to_test_projected.a - grid_line.a).cast<double>().squaredNorm() > (line_to_test_projected.b - grid_line.a).cast<double>().squaredNorm())
                            line_to_test_projected.reverse();

                        painted_lines_set.insert(*it_contour_and_segment);
                        {
                            boost::lock_guard<std::mutex> lock(painted_lines_mutex);
                            painted_lines.push_back({it_contour_and_segment->first, it_contour_and_segment->second, line_to_test_projected, this->color});
                        }
                    }
                }
            }
        }
        // Continue traversing the grid along the edge.
        return true;
    }

    const EdgeGrid::Grid                                                                 &grid;
    std::vector<PaintedLine>                                                             &painted_lines;
    std::mutex                                                                           &painted_lines_mutex;
    Line                                                                                  line_to_test;
    std::unordered_set<std::pair<size_t, size_t>, boost::hash<std::pair<size_t, size_t>>> painted_lines_set;
    int                                                                                   color             = -1;

    static inline const double                                                            cos_threshold2    = Slic3r::sqr(cos(M_PI * 30. / 180.));
    static inline const double                                                            append_threshold  = 50 * SCALED_EPSILON;
    static inline const double                                                            append_threshold2 = Slic3r::sqr(append_threshold);
};

BoundingBox get_extents(const std::vector<ColoredLines> &colored_polygons) {
    BoundingBox bbox;
    for (const ColoredLines &colored_lines : colored_polygons) {
        for (const ColoredLine &colored_line : colored_lines) {
            bbox.merge(colored_line.line.a);
            bbox.merge(colored_line.line.b);
        }
    }
    return bbox;
}

// Flatten the vector of vectors into a vector.
static inline ColoredLines to_lines(const std::vector<ColoredLines> &c_lines)
{
    size_t n_lines = 0;
    for (const auto &c_line : c_lines)
        n_lines += c_line.size();
    ColoredLines lines;
    lines.reserve(n_lines);
    for (const auto &c_line : c_lines)
        lines.insert(lines.end(), c_line.begin(), c_line.end());
    return lines;
}

static std::vector<std::pair<size_t, size_t>> get_segments(const ColoredLines &polygon)
{
    std::vector<std::pair<size_t, size_t>> segments;

    size_t segment_end = 0;
    while (segment_end + 1 < polygon.size() && polygon[segment_end].color == polygon[segment_end + 1].color)
        segment_end++;

    if (segment_end == polygon.size() - 1)
        return {std::make_pair(0, polygon.size() - 1)};

    size_t first_different_color = (segment_end + 1) % polygon.size();
    for (size_t line_offset_idx = 0; line_offset_idx < polygon.size(); ++line_offset_idx) {
        size_t start_s = (first_different_color + line_offset_idx) % polygon.size();
        size_t end_s   = start_s;

        while (line_offset_idx + 1 < polygon.size() && polygon[start_s].color == polygon[(first_different_color + line_offset_idx + 1) % polygon.size()].color) {
            end_s = (first_different_color + line_offset_idx + 1) % polygon.size();
            line_offset_idx++;
        }
        segments.emplace_back(start_s, end_s);
    }
    return segments;
}

static std::vector<PaintedLine> filter_painted_lines(const Line &line_to_process, const size_t start_idx, const size_t end_idx, const std::vector<PaintedLine> &painted_lines)
{
    const int                filter_eps_value = scale_(0.1f);
    std::vector<PaintedLine> filtered_lines;
    filtered_lines.emplace_back(painted_lines[start_idx]);
    for (size_t line_idx = start_idx + 1; line_idx <= end_idx; ++line_idx) {
        // line_to_process is already all colored. Skip another possible duplicate coloring.
        if(filtered_lines.back().projected_line.b == line_to_process.b)
            break;

        PaintedLine &prev = filtered_lines.back();
        const PaintedLine &curr = painted_lines[line_idx];

        double prev_length        = prev.projected_line.length();
        double curr_dist_start    = (curr.projected_line.a - prev.projected_line.a).cast<double>().norm();
        double dist_between_lines = curr_dist_start - prev_length;

        if (dist_between_lines >= 0) {
            if (prev.color == curr.color) {
                if (dist_between_lines <= filter_eps_value) {
                    prev.projected_line.b = curr.projected_line.b;
                } else {
                    filtered_lines.emplace_back(curr);
                }
            } else {
                filtered_lines.emplace_back(curr);
            }
        } else {
            double curr_dist_end = (curr.projected_line.b - prev.projected_line.a).cast<double>().norm();
            if (curr_dist_end > prev_length) {
                if (prev.color == curr.color)
                    prev.projected_line.b = curr.projected_line.b;
                else
                    filtered_lines.push_back({curr.contour_idx, curr.line_idx, Line{prev.projected_line.b, curr.projected_line.b}, curr.color});
            }
        }
    }

    if (double dist_to_start = (filtered_lines.front().projected_line.a - line_to_process.a).cast<double>().norm(); dist_to_start <= filter_eps_value)
        filtered_lines.front().projected_line.a = line_to_process.a;

    if (double dist_to_end = (filtered_lines.back().projected_line.b - line_to_process.b).cast<double>().norm(); dist_to_end <= filter_eps_value)
        filtered_lines.back().projected_line.b = line_to_process.b;

    return filtered_lines;
}

static std::vector<std::vector<PaintedLine>> post_process_painted_lines(const std::vector<EdgeGrid::Contour> &contours, std::vector<PaintedLine> &&painted_lines)
{
    if (painted_lines.empty())
        return {};

    auto comp = [&contours](const PaintedLine &first, const PaintedLine &second) {
        Point first_start_p = contours[first.contour_idx].segment_start(first.line_idx);
        return first.contour_idx < second.contour_idx ||
               (first.contour_idx == second.contour_idx &&
                (first.line_idx < second.line_idx ||
                 (first.line_idx == second.line_idx &&
                  ((first.projected_line.a - first_start_p).cast<double>().squaredNorm() < (second.projected_line.a - first_start_p).cast<double>().squaredNorm() ||
                   ((first.projected_line.a - first_start_p).cast<double>().squaredNorm() == (second.projected_line.a - first_start_p).cast<double>().squaredNorm() &&
                    (first.projected_line.b - first.projected_line.a).cast<double>().squaredNorm() < (second.projected_line.b - second.projected_line.a).cast<double>().squaredNorm())))));
    };
    std::sort(painted_lines.begin(), painted_lines.end(), comp);

    std::vector<std::vector<PaintedLine>> filtered_painted_lines(contours.size());
    size_t prev_painted_line_idx = 0;
    for (size_t curr_painted_line_idx = 0; curr_painted_line_idx < painted_lines.size(); ++curr_painted_line_idx) {
        size_t next_painted_line_idx = curr_painted_line_idx + 1;
        if (next_painted_line_idx >= painted_lines.size() || painted_lines[curr_painted_line_idx].contour_idx != painted_lines[next_painted_line_idx].contour_idx || painted_lines[curr_painted_line_idx].line_idx != painted_lines[next_painted_line_idx].line_idx) {
            const PaintedLine &start_line      = painted_lines[prev_painted_line_idx];
            const Line        &line_to_process = contours[start_line.contour_idx].get_segment(start_line.line_idx);
            Slic3r::append(filtered_painted_lines[painted_lines[curr_painted_line_idx].contour_idx], filter_painted_lines(line_to_process, prev_painted_line_idx, curr_painted_line_idx, painted_lines));
            prev_painted_line_idx = next_painted_line_idx;
        }
    }

    return filtered_painted_lines;
}

#ifndef NDEBUG
static bool are_lines_connected(const ColoredLines &colored_lines)
{
    for (size_t line_idx = 1; line_idx < colored_lines.size(); ++line_idx)
        if (colored_lines[line_idx - 1].line.b != colored_lines[line_idx].line.a)
            return false;
    return true;
}
#endif

static ColoredLines colorize_line(const Line &line_to_process,
                                              const size_t              start_idx,
                                              const size_t              end_idx,
                                              const std::vector<PaintedLine> &painted_contour)
{
    assert(start_idx < painted_contour.size() && end_idx < painted_contour.size() && start_idx <= end_idx);
    assert(std::all_of(painted_contour.begin() + start_idx, painted_contour.begin() + end_idx + 1, [&painted_contour, &start_idx](const auto &p_line) { return painted_contour[start_idx].line_idx == p_line.line_idx; }));

    const int          filter_eps_value = scale_(0.1f);
    ColoredLines       final_lines;
    const PaintedLine &first_line = painted_contour[start_idx];
    if (double dist_to_start = (first_line.projected_line.a - line_to_process.a).cast<double>().norm(); dist_to_start > filter_eps_value)
        final_lines.push_back({Line(line_to_process.a, first_line.projected_line.a), 0});
    final_lines.push_back({first_line.projected_line, first_line.color});

    for (size_t line_idx = start_idx + 1; line_idx <= end_idx; ++line_idx) {
        ColoredLine       &prev = final_lines.back();
        const PaintedLine &curr = painted_contour[line_idx];

        double line_dist = (curr.projected_line.a - prev.line.b).cast<double>().norm();
        if (line_dist <= filter_eps_value) {
            if (prev.color == curr.color) {
                prev.line.b = curr.projected_line.b;
            } else {
                prev.line.b = curr.projected_line.a;
                final_lines.push_back({curr.projected_line, curr.color});
            }
        } else {
            final_lines.push_back({Line(prev.line.b, curr.projected_line.a), 0});
            final_lines.push_back({curr.projected_line, curr.color});
        }
    }

    // If there is non-painted space, then inserts line painted by a default color.
    if (double dist_to_end = (final_lines.back().line.b - line_to_process.b).cast<double>().norm(); dist_to_end > filter_eps_value)
        final_lines.push_back({Line(final_lines.back().line.b, line_to_process.b), 0});

    // Make sure all the lines are connected.
    assert(are_lines_connected(final_lines));

    for (size_t line_idx = 2; line_idx < final_lines.size(); ++line_idx) {
        const ColoredLine &line_0 = final_lines[line_idx - 2];
        ColoredLine       &line_1 = final_lines[line_idx - 1];
        const ColoredLine &line_2 = final_lines[line_idx - 0];

        if (line_0.color == line_2.color && line_0.color != line_1.color)
            if (line_1.line.length() <= scale_(0.2)) line_1.color = line_0.color;
    }

    ColoredLines colored_lines_simple;
    colored_lines_simple.emplace_back(final_lines.front());
    for (size_t line_idx = 1; line_idx < final_lines.size(); ++line_idx) {
        const ColoredLine &line_0 = final_lines[line_idx];

        if (colored_lines_simple.back().color == line_0.color)
            colored_lines_simple.back().line.b = line_0.line.b;
        else
            colored_lines_simple.emplace_back(line_0);
    }

    final_lines = colored_lines_simple;

    if (final_lines.size() > 1)
        if (final_lines.front().color != final_lines[1].color && final_lines.front().line.length() <= scale_(0.2)) {
            final_lines[1].line.a = final_lines.front().line.a;
            final_lines.erase(final_lines.begin());
        }

    if (final_lines.size() > 1)
        if (final_lines.back().color != final_lines[final_lines.size() - 2].color && final_lines.back().line.length() <= scale_(0.2)) {
            final_lines[final_lines.size() - 2].line.b = final_lines.back().line.b;
            final_lines.pop_back();
        }

    return final_lines;
}

static ColoredLines filter_colorized_polygon(ColoredLines &&new_lines) {
    for (size_t line_idx = 2; line_idx < new_lines.size(); ++line_idx) {
        const ColoredLine &line_0 = new_lines[line_idx - 2];
        ColoredLine       &line_1 = new_lines[line_idx - 1];
        const ColoredLine &line_2 = new_lines[line_idx - 0];

        if (line_0.color == line_2.color && line_0.color != line_1.color && line_0.color >= 1) {
            if (line_1.line.length() <= scale_(0.5)) line_1.color = line_0.color;
        }
    }

    for (size_t line_idx = 3; line_idx < new_lines.size(); ++line_idx) {
        const ColoredLine &line_0 = new_lines[line_idx - 3];
        ColoredLine       &line_1 = new_lines[line_idx - 2];
        ColoredLine       &line_2 = new_lines[line_idx - 1];
        const ColoredLine &line_3 = new_lines[line_idx - 0];

        if (line_0.color == line_3.color && (line_0.color != line_1.color || line_0.color != line_2.color) && line_0.color >= 1 && line_3.color >= 1) {
            if ((line_1.line.length() + line_2.line.length()) <= scale_(0.5)) {
                line_1.color = line_0.color;
                line_2.color = line_0.color;
            }
        }
    }

    std::vector<std::pair<size_t, size_t>> segments       = get_segments(new_lines);
    auto                                   segment_length = [&new_lines](const std::pair<size_t, size_t> &segment) {
        double total_length = 0;
        for (size_t seg_start_idx = segment.first; seg_start_idx != segment.second; seg_start_idx = (seg_start_idx + 1 < new_lines.size()) ? seg_start_idx + 1 : 0)
            total_length += new_lines[seg_start_idx].line.length();
        total_length += new_lines[segment.second].line.length();
        return total_length;
    };

    if (segments.size() >= 2)
        for (size_t curr_idx = 0; curr_idx < segments.size(); ++curr_idx) {
            size_t next_idx = next_idx_modulo(curr_idx, segments.size());
            assert(curr_idx != next_idx);

            int color0 = new_lines[segments[curr_idx].first].color;
            int color1 = new_lines[segments[next_idx].first].color;

            double seg0l = segment_length(segments[curr_idx]);
            double seg1l = segment_length(segments[next_idx]);

            if (color0 != color1 && seg0l >= scale_(0.1) && seg1l <= scale_(0.2)) {
                for (size_t seg_start_idx = segments[next_idx].first; seg_start_idx != segments[next_idx].second; seg_start_idx = (seg_start_idx + 1 < new_lines.size()) ? seg_start_idx + 1 : 0)
                    new_lines[seg_start_idx].color = color0;
                new_lines[segments[next_idx].second].color = color0;
            }
        }

    segments = get_segments(new_lines);
    if (segments.size() >= 2)
        for (size_t curr_idx = 0; curr_idx < segments.size(); ++curr_idx) {
            size_t next_idx = next_idx_modulo(curr_idx, segments.size());
            assert(curr_idx != next_idx);

            int    color0 = new_lines[segments[curr_idx].first].color;
            int    color1 = new_lines[segments[next_idx].first].color;
            double seg1l  = segment_length(segments[next_idx]);

            if (color0 >= 1 && color0 != color1 && seg1l <= scale_(0.2)) {
                for (size_t seg_start_idx = segments[next_idx].first; seg_start_idx != segments[next_idx].second; seg_start_idx = (seg_start_idx + 1 < new_lines.size()) ? seg_start_idx + 1 : 0)
                    new_lines[seg_start_idx].color = color0;
                new_lines[segments[next_idx].second].color = color0;
            }
        }

    segments = get_segments(new_lines);
    if (segments.size() >= 3)
        for (size_t curr_idx = 0; curr_idx < segments.size(); ++curr_idx) {
            size_t next_idx      = next_idx_modulo(curr_idx, segments.size());
            size_t next_next_idx = next_idx_modulo(next_idx, segments.size());

            int color0 = new_lines[segments[curr_idx].first].color;
            int color1 = new_lines[segments[next_idx].first].color;
            int color2 = new_lines[segments[next_next_idx].first].color;

            if (color0 > 0 && color0 == color2 && color0 != color1 && segment_length(segments[next_idx]) <= scale_(0.5)) {
                for (size_t seg_start_idx = segments[next_next_idx].first; seg_start_idx != segments[next_next_idx].second; seg_start_idx = (seg_start_idx + 1 < new_lines.size()) ? seg_start_idx + 1 : 0)
                    new_lines[seg_start_idx].color = color0;
                new_lines[segments[next_next_idx].second].color = color0;
            }
        }

    return std::move(new_lines);
}

static ColoredLines colorize_contour(const EdgeGrid::Contour &contour, const std::vector<PaintedLine> &painted_contour) {
    assert(painted_contour.empty() || std::all_of(painted_contour.begin(), painted_contour.end(), [&painted_contour](const auto &p_line) { return painted_contour.front().contour_idx == p_line.contour_idx; }));

    ColoredLines colorized_contour;
    if (painted_contour.empty()) {
        // Appends contour with default color for lines before the first PaintedLine.
        colorized_contour.reserve(contour.num_segments());
        for (const Line &line : contour.get_segments())
            colorized_contour.emplace_back(ColoredLine{line, 0});
        return colorized_contour;
    }

    colorized_contour.reserve(contour.num_segments() + painted_contour.size());
    for (size_t idx = 0; idx < painted_contour.front().line_idx; ++idx)
        colorized_contour.emplace_back(ColoredLine{contour.get_segment(idx), 0});

    size_t prev_painted_line_idx = 0;
    for (size_t curr_painted_line_idx = 0; curr_painted_line_idx < painted_contour.size(); ++curr_painted_line_idx) {
        size_t next_painted_line_idx = curr_painted_line_idx + 1;
        if (next_painted_line_idx >= painted_contour.size() || painted_contour[curr_painted_line_idx].line_idx != painted_contour[next_painted_line_idx].line_idx) {
            const std::vector<PaintedLine> &painted_contour_copy = painted_contour;
            Slic3r::append(colorized_contour, colorize_line(contour.get_segment(painted_contour[prev_painted_line_idx].line_idx), prev_painted_line_idx, curr_painted_line_idx, painted_contour_copy));

            // Appends contour with default color for lines between the current and the next PaintedLine.
            if (next_painted_line_idx < painted_contour.size())
                for (size_t idx = painted_contour[curr_painted_line_idx].line_idx + 1; idx < painted_contour[next_painted_line_idx].line_idx; ++idx)
                    colorized_contour.emplace_back(ColoredLine{contour.get_segment(idx), 0});

            prev_painted_line_idx = next_painted_line_idx;
        }
    }

    // Appends contour with default color for lines after the last PaintedLine.
    for (size_t idx = painted_contour.back().line_idx + 1; idx < contour.num_segments(); ++idx)
        colorized_contour.emplace_back(ColoredLine{contour.get_segment(idx), 0});

    assert(!colorized_contour.empty());
    return filter_colorized_polygon(std::move(colorized_contour));
}

static std::vector<ColoredLines> colorize_contours(const std::vector<EdgeGrid::Contour> &contours, const std::vector<std::vector<PaintedLine>> &painted_contours)
{
    assert(contours.size() == painted_contours.size());
    std::vector<ColoredLines> colorized_contours(contours.size());
    for (const std::vector<PaintedLine> &painted_contour : painted_contours) {
        size_t contour_idx              = &painted_contour - &painted_contours.front();
        colorized_contours[contour_idx] = colorize_contour(contours[contour_idx], painted_contours[contour_idx]);
    }

    size_t poly_idx = 0;
    for (ColoredLines &color_lines : colorized_contours) {
        size_t line_idx = 0;
        for (size_t color_line_idx = 0; color_line_idx < color_lines.size(); ++color_line_idx) {
            color_lines[color_line_idx].poly_idx       = int(poly_idx);
            color_lines[color_line_idx].local_line_idx = int(line_idx);
            ++line_idx;
        }
        ++poly_idx;
    }

    return colorized_contours;
}

// Determines if the line points from the point between two contour lines is pointing inside polygon or outside.
static inline bool points_inside(const Line &contour_first, const Line &contour_second, const Point &new_point)
{
    // TODO: Used in points_inside for decision if line leading thought the common point of two lines is pointing inside polygon or outside
    auto three_points_inward_normal = [](const Point &left, const Point &middle, const Point &right) -> Vec2d {
        assert(left != middle);
        assert(middle != right);
        return (perp(Point(middle - left)).cast<double>().normalized() + perp(Point(right - middle)).cast<double>().normalized()).normalized();
    };

    assert(contour_first.b == contour_second.a);
    Vec2d  inward_normal = three_points_inward_normal(contour_first.a, contour_first.b, contour_second.b);
    Vec2d  edge_norm     = (new_point - contour_first.b).cast<double>().normalized();
    double side          = inward_normal.dot(edge_norm);
    //    assert(side != 0.);
    return side > 0.;
}

enum VD_ANNOTATION : Voronoi::VD::cell_type::color_type {
    VERTEX_ON_CONTOUR = 1,
    DELETED           = 2
};

#ifdef MM_SEGMENTATION_DEBUG_GRAPH
static void export_graph_to_svg(const std::string &path, const Voronoi::VD& vd, const std::vector<ColoredLines>& colored_polygons) {
    const coordf_t                 stroke_width = scaled<coordf_t>(0.05f);
    const BoundingBox              bbox         = get_extents(colored_polygons);

    SVG svg(path.c_str(), bbox);
    for (const ColoredLines &colored_lines : colored_polygons)
        for (const ColoredLine &colored_line : colored_lines)
            svg.draw(colored_line.line, "black", stroke_width);

    for (const Voronoi::VD::vertex_type &vertex : vd.vertices()) {
        if (Geometry::VoronoiUtils::is_in_range<coord_t>(vertex)) {
            if (const Point pt = Geometry::VoronoiUtils::to_point(&vertex).cast<coord_t>(); vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR) {
                svg.draw(pt, "blue", coord_t(stroke_width));
            } else if (vertex.color() != VD_ANNOTATION::DELETED) {
                svg.draw(pt, "green", coord_t(stroke_width));
            }
        }
    }

    for (const Voronoi::VD::edge_type &edge : vd.edges()) {
        if (edge.is_infinite() || !Geometry::VoronoiUtils::is_in_range<coord_t>(edge))
            continue;

        const Point from = Geometry::VoronoiUtils::to_point(edge.vertex0()).cast<coord_t>();
        const Point to   = Geometry::VoronoiUtils::to_point(edge.vertex1()).cast<coord_t>();

        if (edge.color() != VD_ANNOTATION::DELETED)
            svg.draw(Line(from, to), "red", stroke_width);
    }
}
#endif // MM_SEGMENTATION_DEBUG_GRAPH

static size_t non_deleted_edge_count(const VD::vertex_type &vertex) {
    size_t               non_deleted_edge_cnt = 0;
    const VD::edge_type *edge                 = vertex.incident_edge();
    do {
        if (edge->color() != VD_ANNOTATION::DELETED)
            ++non_deleted_edge_cnt;
    } while (edge = edge->prev()->twin(), edge != vertex.incident_edge());

    return non_deleted_edge_cnt;
}

static bool can_vertex_be_deleted(const VD::vertex_type &vertex) {
    if (vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR || vertex.color() == VD_ANNOTATION::DELETED)
        return false;

    return non_deleted_edge_count(vertex) <= 1;
}

static void delete_vertex_deep(const VD::vertex_type &vertex) {
    std::queue<const VD::vertex_type *> vertices_to_delete;
    vertices_to_delete.emplace(&vertex);

    while (!vertices_to_delete.empty()) {
        const VD::vertex_type &vertex_to_delete = *vertices_to_delete.front();
        vertices_to_delete.pop();
        vertex_to_delete.color(VD_ANNOTATION::DELETED);

        const VD::edge_type *edge = vertex_to_delete.incident_edge();
        do {
            edge->color(VD_ANNOTATION::DELETED);
            edge->twin()->color(VD_ANNOTATION::DELETED);

            if (edge->is_finite() && can_vertex_be_deleted(*edge->vertex1()))
                vertices_to_delete.emplace(edge->vertex1());
        } while (edge = edge->prev()->twin(), edge != vertex_to_delete.incident_edge());
    }
}

static inline Vec2d mk_point_vec2d(const VD::vertex_type *point) {
    assert(point != nullptr);
    return {point->x(), point->y()};
}

static inline Vec2d mk_vector_vec2d(const VD::edge_type *edge) {
    assert(edge != nullptr);
    return mk_point_vec2d(edge->vertex1()) - mk_point_vec2d(edge->vertex0());
}

static inline Vec2d mk_flipped_vector_vec2d(const VD::edge_type *edge) {
    assert(edge != nullptr);
    return mk_point_vec2d(edge->vertex0()) - mk_point_vec2d(edge->vertex1());
}

static double edge_length(const VD::edge_type &edge) {
    assert(edge.is_finite());
    return mk_vector_vec2d(&edge).norm();
}

// Used in remove_multiple_edges_in_vertices()
// Returns length of edge with is connected to contour. To this length is include other edges with follows it if they are almost straight (with the
// tolerance of 15) And also if node between two subsequent edges is connected only to these two edges.
static inline double calc_total_edge_length(const VD::edge_type &starting_edge)
{
    double               total_edge_length = edge_length(starting_edge);
    const VD::edge_type *prev              = &starting_edge;
    do {
        if (prev->is_finite() && non_deleted_edge_count(*prev->vertex1()) > 2)
            break;

        bool                 found_next_edge = false;
        const VD::edge_type *current         = prev->next();
        do {
            if (current->color() == VD_ANNOTATION::DELETED)
                continue;

            Vec2d  first_line_vec_n  = mk_flipped_vector_vec2d(prev).normalized();
            Vec2d  second_line_vec_n = mk_vector_vec2d(current).normalized();
            double angle             = ::acos(std::clamp(first_line_vec_n.dot(second_line_vec_n), -1.0, 1.0));
            if (Slic3r::cross2(first_line_vec_n, second_line_vec_n) < 0.0)
                angle = 2.0 * (double) PI - angle;

            if (std::abs(angle - PI) >= (PI / 12))
                continue;

            prev               = current;
            found_next_edge    = true;
            total_edge_length += edge_length(*current);

            break;
        } while (current = current->prev()->twin(), current != prev->next());

        if (!found_next_edge)
            break;

    } while (prev != &starting_edge);

    return total_edge_length;
}

// When a Voronoi vertex has more than one Voronoi edge (for example, in concave parts of a polygon),
// we leave just one Voronoi edge in the Voronoi vertex.
// This Voronoi edge is selected based on a heuristic.
static void remove_multiple_edges_in_vertex(const VD::vertex_type &vertex) {
    if (non_deleted_edge_count(vertex) <= 1)
        return;

    std::vector<std::pair<const VD::edge_type *, double>> edges_to_check;
    const VD::edge_type *edge = vertex.incident_edge();
    do {
        if (edge->color() == VD_ANNOTATION::DELETED)
            continue;

        edges_to_check.emplace_back(edge, calc_total_edge_length(*edge));
    } while (edge = edge->prev()->twin(), edge != vertex.incident_edge());

    std::sort(edges_to_check.begin(), edges_to_check.end(), [](const auto &l, const auto &r) -> bool {
        return l.second > r.second;
    });

    while (edges_to_check.size() > 1) {
        const VD::edge_type &edge_to_check = *edges_to_check.back().first;
        edge_to_check.color(VD_ANNOTATION::DELETED);
        edge_to_check.twin()->color(VD_ANNOTATION::DELETED);

        if (const VD::vertex_type &vertex_to_delete = *edge_to_check.vertex1(); can_vertex_be_deleted(vertex_to_delete))
            delete_vertex_deep(vertex_to_delete);

        edges_to_check.pop_back();
    }
}

// Returns list of ExPolygons for each extruder + 1 for default unpainted regions.
// It iterates through all nodes on the border between two different colors, and from this point,
// start selection always left most edges for every node to construct CCW polygons.
static std::vector<ExPolygons> extract_colored_segments(const std::vector<ColoredLines> &colored_polygons,
                                                        const size_t                     num_extruders,
                                                        const size_t                     layer_idx)
{
    const ColoredLines colored_lines = to_lines(colored_polygons);
    const BoundingBox  bbox          = get_extents(colored_polygons);

    auto get_next_contour_line = [&colored_polygons](const ColoredLine &line) -> const ColoredLine & {
        size_t contour_line_size = colored_polygons[line.poly_idx].size();
        size_t contour_next_idx  = (line.local_line_idx + 1) % contour_line_size;
        return colored_polygons[line.poly_idx][contour_next_idx];
    };

    Voronoi::VD vd;
    vd.construct_voronoi(colored_lines.begin(), colored_lines.end());

    // First, mark each Voronoi vertex on the input polygon to prevent it from being deleted later.
    for (const Voronoi::VD::cell_type &cell : vd.cells()) {
        if (cell.is_degenerate() || !cell.contains_segment())
            continue;

        if (const Geometry::SegmentCellRange<Point> cell_range = Geometry::VoronoiUtils::compute_segment_cell_range(cell, colored_lines.begin(), colored_lines.end()); cell_range.is_valid())
            cell_range.edge_begin->vertex0()->color(VD_ANNOTATION::VERTEX_ON_CONTOUR);
    }

    // Second, remove all Voronoi vertices that are outside the bounding box of input polygons.
    // Such Voronoi vertices are definitely not inside of input polygons, so we don't care about them.
    for (const Voronoi::VD::vertex_type &vertex : vd.vertices()) {
        if (vertex.color() == VD_ANNOTATION::DELETED || vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR)
            continue;

        if (!Geometry::VoronoiUtils::is_in_range<coord_t>(vertex) || !bbox.contains(Geometry::VoronoiUtils::to_point(vertex).cast<coord_t>()))
            delete_vertex_deep(vertex);
    }

    // Third, remove all Voronoi edges that are infinite.
    for (const Voronoi::VD::edge_type &edge : vd.edges()) {
        if (edge.color() != VD_ANNOTATION::DELETED && edge.is_infinite()) {
            edge.color(VD_ANNOTATION::DELETED);
            edge.twin()->color(VD_ANNOTATION::DELETED);

            if (edge.vertex0() != nullptr && can_vertex_be_deleted(*edge.vertex0()))
                delete_vertex_deep(*edge.vertex0());

            if (edge.vertex1() != nullptr && can_vertex_be_deleted(*edge.vertex1()))
                delete_vertex_deep(*edge.vertex1());
        }
    }

    // Fourth, remove all edges that point outward from the input polygon.
    for (Voronoi::VD::cell_type cell : vd.cells()) {
        if (cell.is_degenerate() || !cell.contains_segment())
            continue;

        if (const Geometry::SegmentCellRange<Point> cell_range = Geometry::VoronoiUtils::compute_segment_cell_range(cell, colored_lines.begin(), colored_lines.end()); cell_range.is_valid()) {
            const ColoredLine &current_line = Geometry::VoronoiUtils::get_source_segment(cell, colored_lines.begin(), colored_lines.end());
            const ColoredLine &next_line = get_next_contour_line(current_line);

            const VD::edge_type *edge = cell_range.edge_begin;
            do {
                if (edge->color() == VD_ANNOTATION::DELETED)
                    continue;

                if (!points_inside(current_line.line, next_line.line, Geometry::VoronoiUtils::to_point(edge->vertex1()).cast<coord_t>())) {
                    edge->color(VD_ANNOTATION::DELETED);
                    edge->twin()->color(VD_ANNOTATION::DELETED);
                    delete_vertex_deep(*edge->vertex1());
                }
            } while (edge = edge->prev()->twin(), edge != cell_range.edge_begin);
        }
    }

    // Fifth, if a Voronoi vertex has more than one Voronoi edge, remove all but one of them based on heuristics.
    for (const Voronoi::VD::vertex_type &vertex : vd.vertices()) {
        if (vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR)
            remove_multiple_edges_in_vertex(vertex);
    }

#ifdef MM_SEGMENTATION_DEBUG_GRAPH
    {
        static int iRun = 0;
        export_graph_to_svg(debug_out_path("mm-graph-%d-%d.svg", layer_idx, iRun++), vd, colored_polygons);
    }
#endif // MM_SEGMENTATION_DEBUG_GRAPH

    // Sixth, extract the colored segments from the annotated Voronoi diagram.
    std::vector<ExPolygons> segmented_expolygons_per_extruder(num_extruders + 1);
    for (const Voronoi::VD::cell_type &cell : vd.cells()) {
        if (cell.is_degenerate() || !cell.contains_segment())
            continue;

        if (const Geometry::SegmentCellRange<Point> cell_range = Geometry::VoronoiUtils::compute_segment_cell_range(cell, colored_lines.begin(), colored_lines.end()); cell_range.is_valid()) {
            if (cell_range.edge_begin->vertex0()->color() != VD_ANNOTATION::VERTEX_ON_CONTOUR)
                continue;

            const ColoredLine source_segment = Geometry::VoronoiUtils::get_source_segment(cell, colored_lines.begin(), colored_lines.end());

            Polygon segmented_polygon;
            segmented_polygon.points.emplace_back(source_segment.line.b);

            // We have ensured that each segmented_polygon have to start at edge_begin->vertex0() and end at edge_end->vertex1().
            const VD::edge_type *edge = cell_range.edge_begin;
            do {
                if (edge->color() == VD_ANNOTATION::DELETED)
                    continue;

                const VD::vertex_type &next_vertex = *edge->vertex1();
                segmented_polygon.points.emplace_back(Geometry::VoronoiUtils::to_point(next_vertex).cast<coord_t>());
                edge->color(VD_ANNOTATION::DELETED);

                if (next_vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR || next_vertex.color() == VD_ANNOTATION::DELETED) {
                    assert(next_vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR);
                    break;
                }

                edge = edge->twin();
            } while (edge = edge->twin()->next(), edge != cell_range.edge_begin);

            if (edge->vertex1() != cell_range.edge_end->vertex1())
                continue;

            cell_range.edge_begin->vertex0()->color(VD_ANNOTATION::DELETED);
            segmented_expolygons_per_extruder[source_segment.color].emplace_back(std::move(segmented_polygon));
        }
    }

    // Merge all polygons together for each extruder
    for (auto &segmented_expolygons : segmented_expolygons_per_extruder)
        segmented_expolygons = union_ex(segmented_expolygons);

    return segmented_expolygons_per_extruder;
}

static void cut_segmented_layers(const std::vector<ExPolygons>        &input_expolygons,
                                 std::vector<std::vector<ExPolygons>> &segmented_regions,
                                 const float                           cut_width,
                                 const float                           interlocking_depth,
                                 const std::function<void()>          &throw_on_cancel_callback)
{
    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - cutting segmented layers in parallel - begin";
    const float interlocking_cut_width = interlocking_depth > 0.f ? std::max(cut_width - interlocking_depth, 0.f) : 0.f;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, segmented_regions.size()),[&segmented_regions, &input_expolygons, &cut_width, &interlocking_cut_width, &throw_on_cancel_callback](const tbb::blocked_range<size_t>& range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            const float  region_cut_width       = (layer_idx % 2 == 0 && interlocking_cut_width > 0.f) ? interlocking_cut_width : cut_width;
            const size_t num_extruders_plus_one = segmented_regions[layer_idx].size();
            if (region_cut_width > 0.f) {
                std::vector<ExPolygons> segmented_regions_cuts(num_extruders_plus_one); // Indexed by extruder_id
                for (size_t extruder_idx = 0; extruder_idx < num_extruders_plus_one; ++extruder_idx)
                    if (const ExPolygons &ex_polygons = segmented_regions[layer_idx][extruder_idx]; !ex_polygons.empty())
                        segmented_regions_cuts[extruder_idx] = diff_ex(ex_polygons, offset_ex(input_expolygons[layer_idx], -region_cut_width));
                segmented_regions[layer_idx] = std::move(segmented_regions_cuts);
            }
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - cutting segmented layers in parallel - end";
}

static bool is_volume_sinking(const indexed_triangle_set &its, const Transform3d &trafo)
{
    const Transform3f trafo_f = trafo.cast<float>();
    for (const stl_vertex &vertex : its.vertices)
        if ((trafo_f * vertex).z() < SINKING_Z_THRESHOLD) return true;
    return false;
}

//#define MMU_SEGMENTATION_DEBUG_TOP_BOTTOM

// Returns MMU segmentation of top and bottom layers based on painting in MMU segmentation gizmo
static inline std::vector<std::vector<ExPolygons>> mmu_segmentation_top_and_bottom_layers(const PrintObject             &print_object,
                                                                                          const std::vector<ExPolygons> &input_expolygons,
                                                                                          const std::function<void()>   &throw_on_cancel_callback)
{
    // BBS
    const size_t num_extruders = print_object.print()->config().filament_colour.size() + 1;
    const size_t num_layers    = input_expolygons.size();
    const ConstLayerPtrsAdaptor layers = print_object.layers();

    // Maximum number of top / bottom layers accounts for maximum overlap of one thread group into a neighbor thread group.
    int max_top_layers = 0;
    int max_bottom_layers = 0;
    int granularity = 1;
    for (size_t i = 0; i < print_object.num_printing_regions(); ++ i) {
        const PrintRegionConfig &config = print_object.printing_region(i).config();
        max_top_layers    = std::max(max_top_layers, config.top_shell_layers.value);
        max_bottom_layers = std::max(max_bottom_layers, config.bottom_shell_layers.value);
        granularity       = std::max(granularity, std::max(config.top_shell_layers.value, config.bottom_shell_layers.value) - 1);
    }

    // Project upwards pointing painted triangles over top surfaces,
    // project downards pointing painted triangles over bottom surfaces.
    std::vector<std::vector<Polygons>> top_raw(num_extruders), bottom_raw(num_extruders);
    std::vector<float> zs = zs_from_layers(layers);
    Transform3d        object_trafo = print_object.trafo_centered();

#ifdef MM_SEGMENTATION_DEBUG_TOP_BOTTOM
    static int iRun = 0;
#endif // MM_SEGMENTATION_DEBUG_TOP_BOTTOM

    if (max_top_layers > 0 || max_bottom_layers > 0) {
        for (const ModelVolume *mv : print_object.model_object()->volumes)
            if (mv->is_model_part()) {
                const Transform3d volume_trafo = object_trafo * mv->get_matrix();
                for (size_t extruder_idx = 0; extruder_idx < num_extruders; ++ extruder_idx) {
                    const indexed_triangle_set painted = mv->mmu_segmentation_facets.get_facets_strict(*mv, EnforcerBlockerType(extruder_idx));
#ifdef MM_SEGMENTATION_DEBUG_TOP_BOTTOM
                    {
                        static int iRun = 0;
                        its_write_obj(painted, debug_out_path("mm-painted-patch-%d-%d.obj", iRun ++, extruder_idx).c_str());
                    }
#endif // MM_SEGMENTATION_DEBUG_TOP_BOTTOM
                    if (! painted.indices.empty()) {
                        std::vector<Polygons> top, bottom;
                        if (!zs.empty() && is_volume_sinking(painted, volume_trafo)) {
                            std::vector<float> zs_sinking = {0.f};
                            Slic3r::append(zs_sinking, zs);
                            slice_mesh_slabs(painted, zs_sinking, volume_trafo, max_top_layers > 0 ? &top : nullptr, max_bottom_layers > 0 ? &bottom : nullptr, throw_on_cancel_callback);

                            MeshSlicingParams slicing_params;
                            slicing_params.trafo = volume_trafo;
                            Polygons bottom_slice = slice_mesh(painted, zs[0], slicing_params);

                            top.erase(top.begin());
                            bottom.erase(bottom.begin());

                            bottom[0] = union_(bottom[0], bottom_slice);
                        } else
                            slice_mesh_slabs(painted, zs, volume_trafo, max_top_layers > 0 ? &top : nullptr, max_bottom_layers > 0 ? &bottom : nullptr, throw_on_cancel_callback);
                        auto merge = [](std::vector<Polygons> &&src, std::vector<Polygons> &dst) {
                            auto it_src = find_if(src.begin(), src.end(), [](const Polygons &p){ return ! p.empty(); });
                            if (it_src != src.end()) {
                                if (dst.empty()) {
                                    dst = std::move(src);
                                } else {
                                    assert(src.size() == dst.size());
                                    auto it_dst = dst.begin() + (it_src - src.begin());
                                    for (; it_src != src.end(); ++ it_src, ++ it_dst)
                                        if (! it_src->empty()) {
                                            if (it_dst->empty())
                                                *it_dst = std::move(*it_src);
                                            else
                                                append(*it_dst, std::move(*it_src));
                                        }
                                }
                            }
                        };
                        merge(std::move(top),    top_raw[extruder_idx]);
                        merge(std::move(bottom), bottom_raw[extruder_idx]);
                    }
                }
            }
    }

    auto filter_out_small_polygons = [&num_extruders, &num_layers](std::vector<std::vector<Polygons>> &raw_surfaces, double min_area) -> void {
        for (size_t extruder_idx = 0; extruder_idx < num_extruders; ++extruder_idx)
            if (!raw_surfaces[extruder_idx].empty())
                for (size_t layer_idx = 0; layer_idx < num_layers; ++layer_idx)
                    if (!raw_surfaces[extruder_idx][layer_idx].empty())
                        remove_small(raw_surfaces[extruder_idx][layer_idx], min_area);
    };

    // Filter out polygons less than 0.1mm^2, because they are unprintable and causing dimples on outer primers (#7104)
    filter_out_small_polygons(top_raw, Slic3r::sqr(scale_(0.1f)));
    filter_out_small_polygons(bottom_raw, Slic3r::sqr(scale_(0.1f)));

#ifdef MM_SEGMENTATION_DEBUG_TOP_BOTTOM
    {
        const char* colors[] = { "aqua", "black", "blue", "fuchsia", "gray", "green", "lime", "maroon", "navy", "olive", "purple", "red", "silver", "teal", "yellow" };
        static int iRun = 0;
        for (size_t layer_id = 0; layer_id < zs.size(); ++layer_id) {
            std::vector<std::pair<Slic3r::ExPolygons, SVG::ExPolygonAttributes>> svg;
            for (size_t extruder_idx = 0; extruder_idx < num_extruders; ++ extruder_idx) {
                if (! top_raw[extruder_idx].empty() && ! top_raw[extruder_idx][layer_id].empty())
                    if (ExPolygons expoly = union_ex(top_raw[extruder_idx][layer_id]); ! expoly.empty()) {
                        const char *color = colors[extruder_idx];
                        svg.emplace_back(expoly, SVG::ExPolygonAttributes{ format("top%d", extruder_idx), color, color, color });
                    }
                if (! bottom_raw[extruder_idx].empty() && ! bottom_raw[extruder_idx][layer_id].empty())
                    if (ExPolygons expoly = union_ex(bottom_raw[extruder_idx][layer_id]); ! expoly.empty()) {
                        const char *color = colors[extruder_idx + 8];
                        svg.emplace_back(expoly, SVG::ExPolygonAttributes{ format("bottom%d", extruder_idx), color, color, color });
                    }
            }
            SVG::export_expolygons(debug_out_path("mm-segmentation-top-bottom-%d-%d-%lf.svg", iRun, layer_id, zs[layer_id]), svg);
        }
        ++ iRun;
    }
#endif // MM_SEGMENTATION_DEBUG_TOP_BOTTOM

    // When the upper surface of an object is occluded, it should no longer be considered the upper surface
    {
        for (size_t extruder_idx = 0; extruder_idx < num_extruders; ++extruder_idx) {
            for (size_t layer_idx = 0; layer_idx < layers.size(); ++layer_idx) {
                if (!top_raw[extruder_idx].empty() && !top_raw[extruder_idx][layer_idx].empty() && layer_idx + 1 < layers.size()) {
                    top_raw[extruder_idx][layer_idx] = diff(top_raw[extruder_idx][layer_idx], input_expolygons[layer_idx + 1]);
                }
                if (!bottom_raw[extruder_idx].empty() && !bottom_raw[extruder_idx][layer_idx].empty() && layer_idx > 0) {
                    bottom_raw[extruder_idx][layer_idx] = diff(bottom_raw[extruder_idx][layer_idx], input_expolygons[layer_idx - 1]);
                }
            }
        }
    }

    std::vector<std::vector<ExPolygons>> triangles_by_color_bottom(num_extruders);
    std::vector<std::vector<ExPolygons>> triangles_by_color_top(num_extruders);
    triangles_by_color_bottom.assign(num_extruders, std::vector<ExPolygons>(num_layers * 2));
    triangles_by_color_top.assign(num_extruders, std::vector<ExPolygons>(num_layers * 2));

    // BBS: use shell_triangles_by_color_bottom & shell_triangles_by_color_top to save the top and bottom embedded layers's color information
    std::vector<std::vector<ExPolygons>> shell_triangles_by_color_bottom(num_extruders);
    std::vector<std::vector<ExPolygons>> shell_triangles_by_color_top(num_extruders);
    shell_triangles_by_color_bottom.assign(num_extruders, std::vector<ExPolygons>(num_layers * 2));
    shell_triangles_by_color_top.assign(num_extruders, std::vector<ExPolygons>(num_layers * 2));

    struct LayerColorStat {
        // Number of regions for a queried color.
        int     num_regions             { 0 };
        // Maximum perimeter extrusion width for a queried color.
        float   extrusion_width         { 0.f };
        // Minimum radius of a region to be printable. Used to filter regions by morphological opening.
        float   small_region_threshold  { 0.f };
        // Maximum number of top layers for a queried color.
        int     top_shell_layers        { 0 };
        // Maximum number of bottom layers for a queried color.
        int     bottom_shell_layers     { 0 };
        //BBS: spacing according to width and layer height
        float   extrusion_spacing{ 0.f };
    };
    auto layer_color_stat = [&layers = std::as_const(layers), &print_object](const size_t layer_idx, const size_t color_idx) -> LayerColorStat {
        LayerColorStat out;
        const Layer &layer = *layers[layer_idx];
        for (const LayerRegion *region : layer.regions())
            if (const PrintRegionConfig &config = region->region().config();
                // color_idx == 0 means "don't know" extruder aka the underlying extruder.
                // As this region may split existing regions, we collect statistics over all regions for color_idx == 0.
                color_idx == 0 || config.wall_filament == int(color_idx)) {
                //BBS: the extrusion line width is outer wall rather than inner wall
                const double nozzle_diameter = print_object.print()->config().nozzle_diameter.get_at(0);
                double outer_wall_line_width = config.get_abs_value("outer_wall_line_width", nozzle_diameter);
                out.extrusion_width     = std::max<float>(out.extrusion_width, outer_wall_line_width);
                out.top_shell_layers    = std::max<int>(out.top_shell_layers, config.top_shell_layers);
                out.bottom_shell_layers = std::max<int>(out.bottom_shell_layers, config.bottom_shell_layers);
                out.small_region_threshold = config.gap_infill_speed.value > 0 ?
                                             // Gap fill enabled. Enable a single line of 1/2 extrusion width.
                                             0.5f * outer_wall_line_width :
                                             // Gap fill disabled. Enable two lines slightly overlapping.
                                             outer_wall_line_width + 0.7f * Flow::rounded_rectangle_extrusion_spacing(outer_wall_line_width, float(layer.height));
                out.small_region_threshold = scaled<float>(out.small_region_threshold * 0.5f);
                out.extrusion_spacing = Flow::rounded_rectangle_extrusion_spacing(float(outer_wall_line_width), float(layer.height));
                ++ out.num_regions;
            }
        assert(out.num_regions > 0);
        out.extrusion_width = scaled<float>(out.extrusion_width);
        out.extrusion_spacing = scaled<float>(out.extrusion_spacing);
        return out;
    };

    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers, granularity), [&granularity, &num_layers, &num_extruders, &layer_color_stat, &top_raw, &triangles_by_color_top,
                                                                               &throw_on_cancel_callback, &input_expolygons, &bottom_raw, &triangles_by_color_bottom,
                                                                               &shell_triangles_by_color_top, &shell_triangles_by_color_bottom](const tbb::blocked_range<size_t> &range) {
        size_t group_idx   = range.begin() / granularity;
        size_t layer_idx_offset = (group_idx & 1) * num_layers;
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
            for (size_t color_idx = 0; color_idx < num_extruders; ++ color_idx) {
                throw_on_cancel_callback();
                LayerColorStat stat = layer_color_stat(layer_idx, color_idx);
                if (std::vector<Polygons> &top = top_raw[color_idx]; ! top.empty() && ! top[layer_idx].empty())
                    if (ExPolygons top_ex = union_ex(top[layer_idx]); ! top_ex.empty()) {
                        // Clean up thin projections. They are not printable anyways.
                        top_ex = opening_ex(top_ex, stat.small_region_threshold);
                        if (! top_ex.empty()) {
                            append(triangles_by_color_top[color_idx][layer_idx + layer_idx_offset], top_ex);
                            float offset = 0.f;
                            ExPolygons layer_slices_trimmed = input_expolygons[layer_idx];
                            for (int last_idx = int(layer_idx) - 1; last_idx > std::max(int(layer_idx - stat.top_shell_layers), int(0)); --last_idx) {
                                //BBS: offset width should be 2*spacing to avoid too narrow area which has overlap of wall line
                                //offset -= stat.extrusion_width ;
                                offset -= (stat.extrusion_spacing + stat.extrusion_width);
                                layer_slices_trimmed = intersection_ex(layer_slices_trimmed, input_expolygons[last_idx]);
                                ExPolygons last = opening_ex(intersection_ex(top_ex, offset_ex(layer_slices_trimmed, offset)), stat.small_region_threshold);
                                if (last.empty())
                                    break;
                                append(shell_triangles_by_color_top[color_idx][last_idx + layer_idx_offset], std::move(last));
                            }
                        }
                    }
                if (std::vector<Polygons> &bottom = bottom_raw[color_idx]; ! bottom.empty() && ! bottom[layer_idx].empty())
                    if (ExPolygons bottom_ex = union_ex(bottom[layer_idx]); ! bottom_ex.empty()) {
                        // Clean up thin projections. They are not printable anyways.
                        bottom_ex = opening_ex(bottom_ex, stat.small_region_threshold);
                        if (! bottom_ex.empty()) {
                            append(triangles_by_color_bottom[color_idx][layer_idx + layer_idx_offset], bottom_ex);
                            float offset = 0.f;
                            ExPolygons layer_slices_trimmed = input_expolygons[layer_idx];
                            for (size_t last_idx = layer_idx + 1; last_idx < std::min(layer_idx + stat.bottom_shell_layers, num_layers); ++last_idx) {
                                //BBS: offset width should be 2*spacing to avoid too narrow area which has overlap of wall line
                                //offset -= stat.extrusion_width;
                                offset -= (stat.extrusion_spacing + stat.extrusion_width);
                                layer_slices_trimmed = intersection_ex(layer_slices_trimmed, input_expolygons[last_idx]);
                                ExPolygons last = opening_ex(intersection_ex(bottom_ex, offset_ex(layer_slices_trimmed, offset)), stat.small_region_threshold);
                                if (last.empty())
                                    break;
                                append(shell_triangles_by_color_bottom[color_idx][last_idx + layer_idx_offset], std::move(last));
                            }
                        }
                    }
            }
        }
    });

    std::vector<std::vector<ExPolygons>> triangles_by_color_merged(num_extruders);
    triangles_by_color_merged.assign(num_extruders, std::vector<ExPolygons>(num_layers));
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&triangles_by_color_merged, &triangles_by_color_bottom, &triangles_by_color_top, &num_layers, &throw_on_cancel_callback,
                                                                  &shell_triangles_by_color_top, &shell_triangles_by_color_bottom](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
            throw_on_cancel_callback();
            ExPolygons painted_exploys;
            for (size_t color_idx = 0; color_idx < triangles_by_color_merged.size(); ++color_idx) {
                auto &self = triangles_by_color_merged[color_idx][layer_idx];
                append(self, std::move(triangles_by_color_bottom[color_idx][layer_idx]));
                append(self, std::move(triangles_by_color_bottom[color_idx][layer_idx + num_layers]));
                append(self, std::move(triangles_by_color_top[color_idx][layer_idx]));
                append(self, std::move(triangles_by_color_top[color_idx][layer_idx + num_layers]));
                self = union_ex(self);

                append(painted_exploys, self);
            }

            painted_exploys = union_ex(painted_exploys);

            //BBS: merge the top and bottom shell layers
            for (size_t color_idx = 0; color_idx < triangles_by_color_merged.size(); ++color_idx) {
                auto &self = triangles_by_color_merged[color_idx][layer_idx];

                auto top_area = diff_ex(union_ex(shell_triangles_by_color_top[color_idx][layer_idx],
                                                 shell_triangles_by_color_top[color_idx][layer_idx + num_layers]),
                                        painted_exploys);

                auto bottom_area = diff_ex(union_ex(shell_triangles_by_color_bottom[color_idx][layer_idx],
                                                    shell_triangles_by_color_bottom[color_idx][layer_idx + num_layers]),
                                          painted_exploys);

                append(self, top_area);
                append(self, bottom_area);
                self = union_ex(self);
            }
            // Trim one region by the other if some of the regions overlap.
            ExPolygons painted_regions;
            for (size_t color_idx = 1; color_idx < triangles_by_color_merged.size(); ++color_idx) {
                triangles_by_color_merged[color_idx][layer_idx] = diff_ex(triangles_by_color_merged[color_idx][layer_idx], painted_regions);
                append(painted_regions, triangles_by_color_merged[color_idx][layer_idx]);
            }
            triangles_by_color_merged[0][layer_idx] = diff_ex(triangles_by_color_merged[0][layer_idx], painted_regions);
        }
    });

    return triangles_by_color_merged;
}

static std::vector<std::vector<ExPolygons>> merge_segmented_layers(
    const std::vector<std::vector<ExPolygons>> &segmented_regions,
    std::vector<std::vector<ExPolygons>>     &&top_and_bottom_layers,
    const size_t                               num_extruders,
    const std::function<void()>               &throw_on_cancel_callback)
{
    const size_t                         num_layers = segmented_regions.size();
    std::vector<std::vector<ExPolygons>> segmented_regions_merged(num_layers);
    segmented_regions_merged.assign(num_layers, std::vector<ExPolygons>(num_extruders));
    assert(num_extruders + 1 == top_and_bottom_layers.size());

    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - merging segmented layers in parallel - begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&segmented_regions, &top_and_bottom_layers, &segmented_regions_merged, &num_extruders, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            assert(segmented_regions[layer_idx].size() == num_extruders + 1);
            // Zero is skipped because it is the default color of the volume
            for (size_t extruder_id = 1; extruder_id < num_extruders + 1; ++extruder_id) {
                throw_on_cancel_callback();
                if (!segmented_regions[layer_idx][extruder_id].empty()) {
                    ExPolygons segmented_regions_trimmed = segmented_regions[layer_idx][extruder_id];
                    for (const std::vector<ExPolygons> &top_and_bottom_by_extruder : top_and_bottom_layers)
                        if (!top_and_bottom_by_extruder[layer_idx].empty() && !segmented_regions_trimmed.empty())
                            segmented_regions_trimmed = diff_ex(segmented_regions_trimmed, top_and_bottom_by_extruder[layer_idx]);

                    segmented_regions_merged[layer_idx][extruder_id - 1] = std::move(segmented_regions_trimmed);
                }

                if (!top_and_bottom_layers[extruder_id][layer_idx].empty()) {
                    bool was_top_and_bottom_empty = segmented_regions_merged[layer_idx][extruder_id - 1].empty();
                    append(segmented_regions_merged[layer_idx][extruder_id - 1], top_and_bottom_layers[extruder_id][layer_idx]);

                    // Remove dimples (#7235) appearing after merging side segmentation of the model with tops and bottoms painted layers.
                    if (!was_top_and_bottom_empty)
                        segmented_regions_merged[layer_idx][extruder_id - 1] = offset2_ex(union_ex(segmented_regions_merged[layer_idx][extruder_id - 1]), float(SCALED_EPSILON), -float(SCALED_EPSILON));
                }
            }
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - merging segmented layers in parallel - end";

    return segmented_regions_merged;
}

#ifdef MM_SEGMENTATION_DEBUG_REGIONS
static void export_regions_to_svg(const std::string &path, const std::vector<ExPolygons> &regions, const ExPolygons &lslices)
{
    const std::vector<std::string> colors       = {"blue", "cyan", "red", "orange", "magenta", "pink", "purple", "yellow"};
    coordf_t                       stroke_width = scale_(0.05);
    BoundingBox                    bbox         = get_extents(lslices);
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    svg.draw_outline(lslices, "green", "lime", stroke_width);
    for (const ExPolygons &by_extruder : regions) {
        size_t extrude_idx = &by_extruder - &regions.front();
        if (extrude_idx < int(colors.size()))
            svg.draw(by_extruder, colors[extrude_idx]);
        else
            svg.draw(by_extruder, "black");
    }
}
#endif // MM_SEGMENTATION_DEBUG_REGIONS

#ifdef MM_SEGMENTATION_DEBUG_INPUT
void export_processed_input_expolygons_to_svg(const std::string &path, const LayerRegionPtrs &regions, const ExPolygons &processed_input_expolygons)
{
    coordf_t    stroke_width = scale_(0.05);
    BoundingBox bbox         = get_extents(regions);
    bbox.merge(get_extents(processed_input_expolygons));
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    for (LayerRegion *region : regions)
        for (const Surface &surface : region->slices.surfaces)
            svg.draw_outline(surface, "blue", "cyan", stroke_width);

    svg.draw_outline(processed_input_expolygons, "red", "pink", stroke_width);
}
#endif // MM_SEGMENTATION_DEBUG_INPUT

#ifdef MM_SEGMENTATION_DEBUG_PAINTED_LINES
static void export_painted_lines_to_svg(const std::string &path, const std::vector<std::vector<PaintedLine>> &all_painted_lines, const ExPolygons &lslices)
{
    const std::vector<std::string> colors       = {"blue", "cyan", "red", "orange", "magenta", "pink", "purple", "yellow"};
    coordf_t                       stroke_width = scale_(0.05);
    BoundingBox                    bbox         = get_extents(lslices);
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    for (const Line &line : to_lines(lslices))
        svg.draw(line, "green", stroke_width);

    for (const std::vector<PaintedLine> &painted_lines : all_painted_lines)
        for (const PaintedLine &painted_line : painted_lines)
            svg.draw(painted_line.projected_line, painted_line.color < int(colors.size()) ? colors[painted_line.color] : "black", stroke_width);
}
#endif // MM_SEGMENTATION_DEBUG_PAINTED_LINES

#ifdef MM_SEGMENTATION_DEBUG_COLORIZED_POLYGONS
static void export_colorized_polygons_to_svg(const std::string &path, const std::vector<ColoredLines> &colorized_polygons, const ExPolygons &lslices)
{
    const std::vector<std::string> colors       = {"blue", "cyan", "red", "orange", "magenta", "pink", "purple", "green", "yellow"};
    coordf_t                       stroke_width = scale_(0.05);
    BoundingBox                    bbox         = get_extents(lslices);
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    for (const ColoredLines &colorized_polygon : colorized_polygons)
        for (const ColoredLine &colorized_line : colorized_polygon)
            svg.draw(colorized_line.line, colorized_line.color < int(colors.size())? colors[colorized_line.color] : "black", stroke_width);
}
#endif // MM_SEGMENTATION_DEBUG_COLORIZED_POLYGONS

// Check if all ColoredLine representing a single layer uses the same color.
static bool has_layer_only_one_color(const std::vector<ColoredLines> &colored_polygons)
{
    assert(!colored_polygons.empty());
    assert(!colored_polygons.front().empty());
    int first_line_color = colored_polygons.front().front().color;
    for (const ColoredLines &colored_polygon : colored_polygons)
        for (const ColoredLine &colored_line : colored_polygon)
            if (first_line_color != colored_line.color)
                return false;

    return true;
}

std::vector<std::vector<ExPolygons>> multi_material_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback)
{
    const size_t                          num_extruders = print_object.print()->config().filament_colour.size();
    const size_t                          num_layers    = print_object.layers().size();
    std::vector<std::vector<ExPolygons>>  segmented_regions(num_layers);
    segmented_regions.assign(num_layers, std::vector<ExPolygons>(num_extruders + 1));
    std::vector<std::vector<PaintedLine>> painted_lines(num_layers);
    std::array<std::mutex, 64>            painted_lines_mutex;
    std::vector<EdgeGrid::Grid>           edge_grids(num_layers);
    const ConstLayerPtrsAdaptor           layers = print_object.layers();
    std::vector<ExPolygons>               input_expolygons(num_layers);

    throw_on_cancel_callback();

#ifdef MM_SEGMENTATION_DEBUG
    static int iRun = 0;
#endif // MM_SEGMENTATION_DEBUG

    // Merge all regions and remove small holes
    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - slices preparation in parallel - begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&layers, &input_expolygons, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            ExPolygons ex_polygons;
            for (LayerRegion *region : layers[layer_idx]->regions())
                for (const Surface &surface : region->slices.surfaces)
                    Slic3r::append(ex_polygons, offset_ex(surface.expolygon, float(10 * SCALED_EPSILON)));
            // All expolygons are expanded by SCALED_EPSILON, merged, and then shrunk again by SCALED_EPSILON
            // to ensure that very close polygons will be merged.
            ex_polygons = union_ex(ex_polygons);
            // Remove all expolygons and holes with an area less than 0.1mm^2
            remove_small_and_small_holes(ex_polygons, Slic3r::sqr(scale_(0.1f)));
            // Occasionally, some input polygons contained self-intersections that caused problems with Voronoi diagrams
            // and consequently with the extraction of colored segments by function extract_colored_segments.
            // Calling simplify_polygons removes these self-intersections.
            // Also, occasionally input polygons contained several points very close together (distance between points is 1 or so).
            // Such close points sometimes caused that the Voronoi diagram has self-intersecting edges around these vertices.
            // This consequently leads to issues with the extraction of colored segments by function extract_colored_segments.
            // Calling expolygons_simplify fixed these issues.
            input_expolygons[layer_idx] = remove_duplicates(expolygons_simplify(offset_ex(ex_polygons, -10.f * float(SCALED_EPSILON)), 5 * SCALED_EPSILON), scaled<coord_t>(0.01), PI/6);

#ifdef MM_SEGMENTATION_DEBUG_INPUT
            export_processed_input_expolygons_to_svg(debug_out_path("mm-input-%d-%d.svg", layer_idx, iRun), layers[layer_idx]->regions(), input_expolygons[layer_idx]);
#endif // MM_SEGMENTATION_DEBUG_INPUT
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - slices preparation in parallel - end";

    std::vector<BoundingBox> layer_bboxes(num_layers);
    for (size_t layer_idx = 0; layer_idx < num_layers; ++layer_idx) {
        throw_on_cancel_callback();
        layer_bboxes[layer_idx] = get_extents(layers[layer_idx]->regions());
        layer_bboxes[layer_idx].merge(get_extents(input_expolygons[layer_idx]));
    }

    for (size_t layer_idx = 0; layer_idx < num_layers; ++layer_idx) {
        throw_on_cancel_callback();
        BoundingBox bbox = layer_bboxes[layer_idx];
        // Projected triangles could, in rare cases (as in GH issue #7299), belongs to polygons printed in the previous or the next layer.
        // Let's merge the bounding box of the current layer with bounding boxes of the previous and the next layer to ensure that
        // every projected triangle will be inside the resulting bounding box.
        if (layer_idx > 1) bbox.merge(layer_bboxes[layer_idx - 1]);
        if (layer_idx < num_layers - 1) bbox.merge(layer_bboxes[layer_idx + 1]);
        // Projected triangles may slightly exceed the input polygons.
        bbox.offset(20 * SCALED_EPSILON);
        edge_grids[layer_idx].set_bbox(bbox);
        edge_grids[layer_idx].create(input_expolygons[layer_idx], coord_t(scale_(10.)));
    }

    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - projection of painted triangles - begin";
    for (const ModelVolume *mv : print_object.model_object()->volumes) {
        tbb::parallel_for(tbb::blocked_range<size_t>(1, num_extruders + 1), [&mv, &print_object, &layers, &edge_grids, &painted_lines, &painted_lines_mutex, &input_expolygons, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
            for (size_t extruder_idx = range.begin(); extruder_idx < range.end(); ++extruder_idx) {
                throw_on_cancel_callback();
                const indexed_triangle_set custom_facets = mv->mmu_segmentation_facets.get_facets(*mv, EnforcerBlockerType(extruder_idx));
                if (!mv->is_model_part() || custom_facets.indices.empty())
                    continue;

                const Transform3f tr = print_object.trafo().cast<float>() * mv->get_matrix().cast<float>();
                tbb::parallel_for(tbb::blocked_range<size_t>(0, custom_facets.indices.size()), [&tr, &custom_facets, &print_object, &layers, &edge_grids, &input_expolygons, &painted_lines, &painted_lines_mutex, &extruder_idx](const tbb::blocked_range<size_t> &range) {
                    for (size_t facet_idx = range.begin(); facet_idx < range.end(); ++facet_idx) {
                        float min_z = std::numeric_limits<float>::max();
                        float max_z = std::numeric_limits<float>::lowest();

                        std::array<Vec3f, 3> facet;
                        for (int p_idx = 0; p_idx < 3; ++p_idx) {
                            facet[p_idx] = tr * custom_facets.vertices[custom_facets.indices[facet_idx](p_idx)];
                            max_z        = std::max(max_z, facet[p_idx].z());
                            min_z        = std::min(min_z, facet[p_idx].z());
                        }

                        if (is_equal(min_z, max_z))
                            continue;

                        // Sort the vertices by z-axis for simplification of projected_facet on slices
                        std::sort(facet.begin(), facet.end(), [](const Vec3f &p1, const Vec3f &p2) { return p1.z() < p2.z(); });

                        // Find lowest slice not below the triangle.
                        auto first_layer = std::upper_bound(layers.begin(), layers.end(), float(min_z - EPSILON),
                                                            [](float z, const Layer *l1) { return z < l1->slice_z; });
                        auto last_layer  = std::upper_bound(layers.begin(), layers.end(), float(max_z + EPSILON),
                                                           [](float z, const Layer *l1) { return z < l1->slice_z; });
                        --last_layer;

                        for (auto layer_it = first_layer; layer_it != (last_layer + 1); ++layer_it) {
                            const Layer *layer     = *layer_it;
                            size_t       layer_idx = layer_it - layers.begin();
                            if (input_expolygons[layer_idx].empty() || is_less(layer->slice_z, facet[0].z()) || is_less(facet[2].z(), layer->slice_z))
                                continue;

                            // https://kandepet.com/3d-printing-slicing-3d-objects/
                            float t            = (float(layer->slice_z) - facet[0].z()) / (facet[2].z() - facet[0].z());
                            Vec3f line_start_f = facet[0] + t * (facet[2] - facet[0]);
                            Vec3f line_end_f;

                            // BBS: When one side of a triangle coincides with the slice_z.
                            if ((is_equal(facet[0].z(), facet[1].z()) && is_equal(facet[1].z(), layer->slice_z))
                                || (is_equal(facet[1].z(), facet[2].z()) && is_equal(facet[1].z(), layer->slice_z))) {
                                line_end_f = facet[1];
                            }
                            else if (facet[1].z() > layer->slice_z) {
                                // [P0, P2] and [P0, P1]
                                float t1   = (float(layer->slice_z) - facet[0].z()) / (facet[1].z() - facet[0].z());
                                line_end_f = facet[0] + t1 * (facet[1] - facet[0]);
                            } else {
                                // [P0, P2] and [P1, P2]
                                float t2   = (float(layer->slice_z) - facet[1].z()) / (facet[2].z() - facet[1].z());
                                line_end_f = facet[1] + t2 * (facet[2] - facet[1]);
                            }

                            Line line_to_test(Point(scale_(line_start_f.x()), scale_(line_start_f.y())),
                                              Point(scale_(line_end_f.x()), scale_(line_end_f.y())));
                            line_to_test.translate(-print_object.center_offset());

                            // BoundingBoxes for EdgeGrids are computed from printable regions. It is possible that the painted line (line_to_test) could
                            // be outside EdgeGrid's BoundingBox, for example, when the negative volume is used on the painted area (GH #7618).
                            // To ensure that the painted line is always inside EdgeGrid's BoundingBox, it is clipped by EdgeGrid's BoundingBox in cases
                            // when any of the endpoints of the line are outside the EdgeGrid's BoundingBox.
                            BoundingBox edge_grid_bbox = edge_grids[layer_idx].bbox();
                            edge_grid_bbox.offset(10 * scale_(EPSILON));
                            if (!edge_grid_bbox.contains(line_to_test.a) || !edge_grid_bbox.contains(line_to_test.b)) {
                                // If the painted line (line_to_test) is entirely outside EdgeGrid's BoundingBox, skip this painted line.
                                if (!edge_grid_bbox.overlap(BoundingBox(Points{line_to_test.a, line_to_test.b})) ||
                                    !line_to_test.clip_with_bbox(edge_grid_bbox))
                                    continue;
                            }

                            size_t mutex_idx = layer_idx & 0x3F;
                            assert(mutex_idx < painted_lines_mutex.size());

                            PaintedLineVisitor visitor(edge_grids[layer_idx], painted_lines[layer_idx], painted_lines_mutex[mutex_idx], 16);
                            visitor.line_to_test = line_to_test;
                            visitor.color        = int(extruder_idx);
                            edge_grids[layer_idx].visit_cells_intersecting_line(line_to_test.a, line_to_test.b, visitor);
                        }
                    }
                }); // end of parallel_for
            }
        }); // end of parallel_for
    }
    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - projection of painted triangles - end";
    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - painted layers count: "
                             << std::count_if(painted_lines.begin(), painted_lines.end(), [](const std::vector<PaintedLine> &pl) { return !pl.empty(); });

    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - layers segmentation in parallel - begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&edge_grids, &input_expolygons, &painted_lines, &segmented_regions, &num_extruders, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            if (!painted_lines[layer_idx].empty()) {
#ifdef MM_SEGMENTATION_DEBUG_PAINTED_LINES
                export_painted_lines_to_svg(debug_out_path("0-mm-painted-lines-%d-%d.svg", layer_idx, iRun), {painted_lines[layer_idx]}, input_expolygons[layer_idx]);
#endif // MM_SEGMENTATION_DEBUG_PAINTED_LINES

                std::vector<std::vector<PaintedLine>> post_processed_painted_lines = post_process_painted_lines(edge_grids[layer_idx].contours(), std::move(painted_lines[layer_idx]));

#ifdef MM_SEGMENTATION_DEBUG_PAINTED_LINES
                export_painted_lines_to_svg(debug_out_path("1-mm-painted-lines-post-processed-%d-%d.svg", layer_idx, iRun), post_processed_painted_lines, input_expolygons[layer_idx]);
#endif // MM_SEGMENTATION_DEBUG_PAINTED_LINES

                std::vector<ColoredLines> color_poly = colorize_contours(edge_grids[layer_idx].contours(), post_processed_painted_lines);

#ifdef MM_SEGMENTATION_DEBUG_COLORIZED_POLYGONS
                export_colorized_polygons_to_svg(debug_out_path("2-mm-colorized_polygons-%d-%d.svg", layer_idx, iRun), color_poly, input_expolygons[layer_idx]);
#endif // MM_SEGMENTATION_DEBUG_COLORIZED_POLYGONS

                assert(!color_poly.empty());
                assert(!color_poly.front().empty());
                if (has_layer_only_one_color(color_poly)) {
                    // If the whole layer is painted using the same color, it is not needed to construct a Voronoi diagram for the segmentation of this layer.
                    segmented_regions[layer_idx][size_t(color_poly.front().front().color)] = input_expolygons[layer_idx];
                } else {
                    segmented_regions[layer_idx] = extract_colored_segments(color_poly, num_extruders, layer_idx);
                }

#ifdef MM_SEGMENTATION_DEBUG_REGIONS
                export_regions_to_svg(debug_out_path("3-mm-regions-sides-%d-%d.svg", layer_idx, iRun), segmented_regions[layer_idx], input_expolygons[layer_idx]);
#endif // MM_SEGMENTATION_DEBUG_REGIONS
            }
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - layers segmentation in parallel - end";
    throw_on_cancel_callback();

    if (auto max_width = print_object.config().mmu_segmented_region_max_width, interlocking_depth = print_object.config().mmu_segmented_region_interlocking_depth; max_width > 0.f) {
        cut_segmented_layers(input_expolygons, segmented_regions, float(scale_(max_width)), float(scale_(interlocking_depth)), throw_on_cancel_callback);
        throw_on_cancel_callback();
    }

    // The first index is extruder number (includes default extruder), and the second one is layer number
    std::vector<std::vector<ExPolygons>> top_and_bottom_layers = mmu_segmentation_top_and_bottom_layers(print_object, input_expolygons, throw_on_cancel_callback);
    throw_on_cancel_callback();

    std::vector<std::vector<ExPolygons>> segmented_regions_merged = merge_segmented_layers(segmented_regions, std::move(top_and_bottom_layers), num_extruders, throw_on_cancel_callback);
    throw_on_cancel_callback();

#ifdef MM_SEGMENTATION_DEBUG_REGIONS
    for (size_t layer_idx = 0; layer_idx < print_object.layers().size(); ++layer_idx)
        export_regions_to_svg(debug_out_path("4-mm-regions-merged-%d-%d.svg", layer_idx, iRun), segmented_regions_merged[layer_idx], input_expolygons[layer_idx]);
#endif // MM_SEGMENTATION_DEBUG_REGIONS

#ifdef MM_SEGMENTATION_DEBUG
    ++iRun;
#endif // MM_SEGMENTATION_DEBUG

    return segmented_regions_merged;
}

} // namespace Slic3r
