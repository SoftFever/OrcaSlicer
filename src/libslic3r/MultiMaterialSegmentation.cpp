#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "EdgeGrid.hpp"
#include "Layer.hpp"
#include "Print.hpp"
#include "VoronoiVisualUtils.hpp"
#include "MutablePolygon.hpp"
#include "format.hpp"

#include <utility>
#include <cfloat>
#include <unordered_set>

#include <boost/log/trivial.hpp>
#include <tbb/parallel_for.h>
#include <mutex>
#include <boost/thread/lock_guard.hpp>

namespace Slic3r {
struct ColoredLine {
    Line line;
    int color;
    int poly_idx = -1;
    int local_line_idx = -1;
};
}

#include <boost/polygon/polygon.hpp>
namespace boost::polygon {
template <>
struct geometry_concept<Slic3r::ColoredLine> { typedef segment_concept type; };

template <>
struct segment_traits<Slic3r::ColoredLine> {
    typedef coord_t coordinate_type;
    typedef Slic3r::Point point_type;

    static inline point_type get(const Slic3r::ColoredLine& line, const direction_1d& dir) {
        return dir.to_int() ? line.line.b : line.line.a;
    }
};
}

//#define MMU_SEGMENTATION_DEBUG_GRAPH
//#define MMU_SEGMENTATION_DEBUG_REGIONS
//#define MMU_SEGMENTATION_DEBUG_INPUT

namespace Slic3r {

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

static std::vector<ColoredLine> to_colored_lines(const EdgeGrid::Contour &contour, int color)
{
    std::vector<ColoredLine> lines;
    if (contour.num_segments() > 2) {
        lines.reserve(contour.num_segments());
        for (auto it = contour.begin(); it != contour.end() - 1; ++it)
            lines.push_back({Line(*it, *(it + 1)), color});
        lines.push_back({Line(contour.back(), contour.front()), color});
    }
    return lines;
}

static Polygon colored_points_to_polygon(const std::vector<ColoredLine> &lines)
{
    Polygon out;
    out.points.reserve(lines.size());
    for (const ColoredLine &l : lines)
        out.points.emplace_back(l.line.a);
    return out;
}

static Polygons colored_points_to_polygon(const std::vector<std::vector<ColoredLine>> &lines)
{
    Polygons out;
    out.reserve(lines.size());
    for (const std::vector<ColoredLine> &l : lines)
        out.emplace_back(colored_points_to_polygon(l));
    return out;
}

// Flatten the vector of vectors into a vector.
static inline std::vector<ColoredLine> to_lines(const std::vector<std::vector<ColoredLine>> &c_lines)
{
    size_t n_lines = 0;
    for (const auto &c_line : c_lines)
        n_lines += c_line.size();
    std::vector<ColoredLine> lines;
    lines.reserve(n_lines);
    for (const auto &c_line : c_lines)
        lines.insert(lines.end(), c_line.begin(), c_line.end());
    return lines;
}

static bool vertex_equal_to_point(const Voronoi::VD::vertex_type &vertex, const Vec2d &ipt)
{
    // Convert ipt to doubles, force the 80bit FPU temporary to 64bit and then compare.
    // This should work with any settings of math compiler switches and the C++ compiler
    // shall understand the memcpies as type punning and it shall optimize them out.
    using ulp_cmp_type = boost::polygon::detail::ulp_comparison<double>;
    ulp_cmp_type ulp_cmp;
    static constexpr int ULPS = boost::polygon::voronoi_diagram_traits<double>::vertex_equality_predicate_type::ULPS;
    return ulp_cmp(vertex.x(), ipt.x(), ULPS) == ulp_cmp_type::EQUAL &&
           ulp_cmp(vertex.y(), ipt.y(), ULPS) == ulp_cmp_type::EQUAL;
}

static inline bool vertex_equal_to_point(const Voronoi::VD::vertex_type *vertex, const Vec2d &ipt)
{
    return vertex_equal_to_point(*vertex, ipt);
}

static std::vector<std::pair<size_t, size_t>> get_segments(const std::vector<ColoredLine> &polygon)
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

static std::vector<std::vector<std::pair<size_t, size_t>>> get_all_segments(const std::vector<std::vector<ColoredLine>> &color_poly)
{
    std::vector<std::vector<std::pair<size_t, size_t>>> all_segments(color_poly.size());
    for (size_t poly_idx = 0; poly_idx < color_poly.size(); ++poly_idx) {
        const std::vector<ColoredLine> &c_polygon = color_poly[poly_idx];
        all_segments[poly_idx]                    = get_segments(c_polygon);
    }
    return all_segments;
}

static std::vector<ColoredLine> colorize_line(const Line &              line_to_process,
                                              const size_t              start_idx,
                                              const size_t              end_idx,
                                              std::vector<PaintedLine> &painted_lines)
{
    std::vector<PaintedLine> internal_painted;
    for (size_t line_idx = start_idx; line_idx <= end_idx; ++line_idx)
        internal_painted.emplace_back(painted_lines[line_idx]);

    const int                filter_eps_value = scale_(0.1f);
    std::vector<PaintedLine> filtered_lines;
    filtered_lines.emplace_back(internal_painted.front());
    for (size_t line_idx = 1; line_idx < internal_painted.size(); ++line_idx) {
        // line_to_process is already all colored. Skip another possible duplicate coloring.
        if(filtered_lines.back().projected_line.b == line_to_process.b)
            break;

        PaintedLine &prev = filtered_lines.back();
        PaintedLine &curr = internal_painted[line_idx];

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
            if (curr_dist_end <= prev_length) {
            } else {
                if (prev.color == curr.color) {
                    prev.projected_line.b = curr.projected_line.b;
                } else {
                    curr.projected_line.a = prev.projected_line.b;
                    filtered_lines.emplace_back(curr);
                }
            }
        }
    }

    std::vector<ColoredLine> final_lines;
    double                   dist_to_start = (filtered_lines.front().projected_line.a - line_to_process.a).cast<double>().norm();
    if (dist_to_start <= filter_eps_value) {
        filtered_lines.front().projected_line.a = line_to_process.a;
        final_lines.push_back({filtered_lines.front().projected_line, filtered_lines.front().color});
    } else {
        final_lines.push_back({Line(line_to_process.a, filtered_lines.front().projected_line.a), 0});
        final_lines.push_back({filtered_lines.front().projected_line, filtered_lines.front().color});
    }

    for (size_t line_idx = 1; line_idx < filtered_lines.size(); ++line_idx) {
        ColoredLine &prev = final_lines.back();
        PaintedLine &curr = filtered_lines[line_idx];

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

    double dist_to_end = (final_lines.back().line.b - line_to_process.b).cast<double>().norm();
    if (dist_to_end <= filter_eps_value)
        final_lines.back().line.b = line_to_process.b;
    else
        final_lines.push_back({Line(final_lines.back().line.b, line_to_process.b), 0});

    for (size_t line_idx = 1; line_idx < final_lines.size(); ++line_idx)
        assert(final_lines[line_idx - 1].line.b == final_lines[line_idx].line.a);

    for (size_t line_idx = 2; line_idx < final_lines.size(); ++line_idx) {
        const ColoredLine &line_0 = final_lines[line_idx - 2];
        ColoredLine &      line_1 = final_lines[line_idx - 1];
        const ColoredLine &line_2 = final_lines[line_idx - 0];

        if (line_0.color == line_2.color && line_0.color != line_1.color)
            if (line_1.line.length() <= scale_(0.2)) line_1.color = line_0.color;
    }

    std::vector<ColoredLine> colored_lines_simple;
    colored_lines_simple.emplace_back(final_lines.front());
    for (size_t line_idx = 1; line_idx < final_lines.size(); ++line_idx) {
        const ColoredLine &line_0 = final_lines[line_idx];

        if (colored_lines_simple.back().color == line_0.color)
            colored_lines_simple.back().line.b = line_0.line.b;
        else
            colored_lines_simple.emplace_back(line_0);
    }

    final_lines = colored_lines_simple;

    if (final_lines.size() > 1) {
        if (final_lines.front().color != final_lines[1].color && final_lines.front().line.length() <= scale_(0.2)) {
            final_lines[1].line.a = final_lines.front().line.a;
            final_lines.erase(final_lines.begin());
        }
    }

    if (final_lines.size() > 1) {
        if (final_lines.back().color != final_lines[final_lines.size() - 2].color && final_lines.back().line.length() <= scale_(0.2)) {
            final_lines[final_lines.size() - 2].line.b = final_lines.back().line.b;
            final_lines.pop_back();
        }
    }

    return final_lines;
}

static std::vector<ColoredLine> colorize_polygon(const EdgeGrid::Contour &contour, const size_t start_idx, const size_t end_idx, std::vector<PaintedLine> &painted_lines)
{
    std::vector<ColoredLine> new_lines;
    new_lines.reserve(end_idx - start_idx + 1);
    for (size_t idx = 0; idx < painted_lines[start_idx].line_idx; ++idx)
        new_lines.emplace_back(ColoredLine{contour.get_segment(idx), 0});

    for (size_t first_idx = start_idx; first_idx <= end_idx; ++first_idx) {
        size_t second_idx = first_idx;
        while (second_idx <= end_idx && painted_lines[first_idx].line_idx == painted_lines[second_idx].line_idx) ++second_idx;
        --second_idx;

        assert(painted_lines[first_idx].line_idx == painted_lines[second_idx].line_idx);
        std::vector<ColoredLine> lines_c_line = colorize_line(contour.get_segment(painted_lines[first_idx].line_idx), first_idx, second_idx, painted_lines);
        new_lines.insert(new_lines.end(), lines_c_line.begin(), lines_c_line.end());

        if (second_idx + 1 <= end_idx)
            for (size_t idx = painted_lines[second_idx].line_idx + 1; idx < painted_lines[second_idx + 1].line_idx; ++idx)
                new_lines.emplace_back(ColoredLine{contour.get_segment(idx), 0});

        first_idx = second_idx;
    }

    for (size_t idx = painted_lines[end_idx].line_idx + 1; idx < contour.num_segments(); ++idx)
        new_lines.emplace_back(ColoredLine{contour.get_segment(idx), 0});

    for (size_t line_idx = 2; line_idx < new_lines.size(); ++line_idx) {
        const ColoredLine &line_0 = new_lines[line_idx - 2];
        ColoredLine &      line_1 = new_lines[line_idx - 1];
        const ColoredLine &line_2 = new_lines[line_idx - 0];

        if (line_0.color == line_2.color && line_0.color != line_1.color && line_0.color >= 1) {
            if (line_1.line.length() <= scale_(0.5)) line_1.color = line_0.color;
        }
    }

    for (size_t line_idx = 3; line_idx < new_lines.size(); ++line_idx) {
        const ColoredLine &line_0 = new_lines[line_idx - 3];
        ColoredLine &      line_1 = new_lines[line_idx - 2];
        ColoredLine &      line_2 = new_lines[line_idx - 1];
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

    for (size_t pair_idx = 1; pair_idx < segments.size(); ++pair_idx) {
        int color0 = new_lines[segments[pair_idx - 1].first].color;
        int color1 = new_lines[segments[pair_idx - 0].first].color;

        double seg0l = segment_length(segments[pair_idx - 1]);
        double seg1l = segment_length(segments[pair_idx - 0]);

        if (color0 != color1 && seg0l >= scale_(0.1) && seg1l <= scale_(0.2)) {
            for (size_t seg_start_idx = segments[pair_idx].first; seg_start_idx != segments[pair_idx].second; seg_start_idx = (seg_start_idx + 1 < new_lines.size()) ? seg_start_idx + 1 : 0)
                new_lines[seg_start_idx].color = color0;
            new_lines[segments[pair_idx].second].color = color0;
        }
    }

    segments = get_segments(new_lines);
    for (size_t pair_idx = 1; pair_idx < segments.size(); ++pair_idx) {
        int    color0 = new_lines[segments[pair_idx - 1].first].color;
        int    color1 = new_lines[segments[pair_idx - 0].first].color;
        double seg1l  = segment_length(segments[pair_idx - 0]);

        if (color0 >= 1 && color0 != color1 && seg1l <= scale_(0.2)) {
            for (size_t seg_start_idx = segments[pair_idx].first; seg_start_idx != segments[pair_idx].second; seg_start_idx = (seg_start_idx + 1 < new_lines.size()) ? seg_start_idx + 1 : 0)
                new_lines[seg_start_idx].color = color0;
            new_lines[segments[pair_idx].second].color = color0;
        }
    }

    for (size_t pair_idx = 2; pair_idx < segments.size(); ++pair_idx) {
        int color0 = new_lines[segments[pair_idx - 2].first].color;
        int color1 = new_lines[segments[pair_idx - 1].first].color;
        int color2 = new_lines[segments[pair_idx - 0].first].color;

        if (color0 > 0 && color0 == color2 && color0 != color1 && segment_length(segments[pair_idx - 1]) <= scale_(0.5)) {
            for (size_t seg_start_idx = segments[pair_idx].first; seg_start_idx != segments[pair_idx].second; seg_start_idx = (seg_start_idx + 1 < new_lines.size()) ? seg_start_idx + 1 : 0)
                new_lines[seg_start_idx].color = color0;
            new_lines[segments[pair_idx].second].color = color0;
        }
    }

    return new_lines;
}

static std::vector<std::vector<ColoredLine>> colorize_polygons(const std::vector<EdgeGrid::Contour> &contours, std::vector<PaintedLine> &painted_lines)
{
    const size_t start_idx = 0;
    const size_t end_idx   = painted_lines.size() - 1;

    std::vector<std::vector<ColoredLine>> new_polygons;
    new_polygons.reserve(contours.size());

    for (size_t idx = 0; idx < painted_lines[start_idx].contour_idx; ++idx)
        new_polygons.emplace_back(to_colored_lines(contours[idx], 0));

    for (size_t first_idx = start_idx; first_idx <= end_idx; ++first_idx) {
        size_t second_idx = first_idx;
        while (second_idx <= end_idx && painted_lines[first_idx].contour_idx == painted_lines[second_idx].contour_idx)
            ++second_idx;
        --second_idx;

        assert(painted_lines[first_idx].contour_idx == painted_lines[second_idx].contour_idx);
        new_polygons.emplace_back(colorize_polygon(contours[painted_lines[first_idx].contour_idx], first_idx, second_idx, painted_lines));

        if (second_idx + 1 <= end_idx)
            for (size_t idx = painted_lines[second_idx].contour_idx + 1; idx < painted_lines[second_idx + 1].contour_idx; ++idx)
                new_polygons.emplace_back(to_colored_lines(contours[idx], 0));

        first_idx = second_idx;
    }

    for (size_t idx = painted_lines[end_idx].contour_idx + 1; idx < contours.size(); ++idx)
        new_polygons.emplace_back(to_colored_lines(contours[idx], 0));

    return new_polygons;
}

using boost::polygon::voronoi_diagram;

static inline Point mk_point(const Voronoi::VD::vertex_type *point) { return {coord_t(point->x()), coord_t(point->y())}; }

static inline Point mk_point(const Voronoi::Internal::point_type &point) { return {coord_t(point.x()), coord_t(point.y())}; }

static inline Point mk_point(const voronoi_diagram<double>::vertex_type &point) { return {coord_t(point.x()), coord_t(point.y())}; }

static inline Point mk_point(const Vec2d &point) { return {coord_t(std::round(point.x())), coord_t(std::round(point.y()))}; }

static inline Vec2d mk_vec2(const voronoi_diagram<double>::vertex_type *point) { return {point->x(), point->y()}; }

struct MMU_Graph
{
    enum class ARC_TYPE { BORDER, NON_BORDER };

    struct Arc
    {
        size_t   from_idx;
        size_t   to_idx;
        int      color;
        ARC_TYPE type;

        bool operator==(const Arc &rhs) const { return (from_idx == rhs.from_idx) && (to_idx == rhs.to_idx) && (color == rhs.color) && (type == rhs.type); }
        bool operator!=(const Arc &rhs) const { return !operator==(rhs); }
    };

    struct Node
    {
        Vec2d             point;
        std::list<size_t> arc_idxs;

        void remove_edge(const size_t to_idx, MMU_Graph &graph)
        {
            for (auto arc_it = this->arc_idxs.begin(); arc_it != this->arc_idxs.end(); ++arc_it) {
                MMU_Graph::Arc &arc = graph.arcs[*arc_it];
                if (arc.to_idx == to_idx) {
                    assert(arc.type != ARC_TYPE::BORDER);
                    this->arc_idxs.erase(arc_it);
                    break;
                }
            }
        }
    };

    std::vector<MMU_Graph::Node> nodes;
    std::vector<MMU_Graph::Arc>  arcs;
    size_t                       all_border_points{};

    std::vector<size_t> polygon_idx_offset;
    std::vector<size_t> polygon_sizes;

    void remove_edge(const size_t from_idx, const size_t to_idx)
    {
        nodes[from_idx].remove_edge(to_idx, *this);
        nodes[to_idx].remove_edge(from_idx, *this);
    }

    [[nodiscard]] size_t get_global_index(const size_t poly_idx, const size_t point_idx) const { return polygon_idx_offset[poly_idx] + point_idx; }

    void append_edge(const size_t &from_idx, const size_t &to_idx, int color = -1, ARC_TYPE type = ARC_TYPE::NON_BORDER)
    {
        // Don't append duplicate edges between the same nodes.
        for (const size_t &arc_idx : this->nodes[from_idx].arc_idxs)
            if (arcs[arc_idx].to_idx == to_idx)
                return;
        for (const size_t &arc_idx : this->nodes[to_idx].arc_idxs)
            if (arcs[arc_idx].to_idx == to_idx)
                return;

        this->nodes[from_idx].arc_idxs.push_back(this->arcs.size());
        this->arcs.push_back({from_idx, to_idx, color, type});

        // Always insert only one directed arc for the input polygons.
        // Two directed arcs in both directions are inserted if arcs aren't between points of the input polygons.
        if (type == ARC_TYPE::NON_BORDER) {
            this->nodes[to_idx].arc_idxs.push_back(this->arcs.size());
            this->arcs.push_back({to_idx, from_idx, color, type});
        }
    }

    // It assumes that between points of the input polygons is always only one directed arc,
    // with the same direction as lines of the input polygon.
    [[nodiscard]] MMU_Graph::Arc get_border_arc(size_t idx) const {
        assert(idx < this->all_border_points);
        return this->arcs[idx];
    }

    [[nodiscard]] size_t nodes_count() const { return this->nodes.size(); }

    void remove_nodes_with_one_arc()
    {
        std::queue<size_t> update_queue;
        for (const MMU_Graph::Node &node : this->nodes) {
            size_t node_idx = &node - &this->nodes.front();
            // Skip nodes that represent points of input polygons.
            if (node.arc_idxs.size() == 1 && node_idx >= this->all_border_points)
                update_queue.emplace(&node - &this->nodes.front());
        }

        while (!update_queue.empty()) {
            size_t           node_from_idx = update_queue.front();
            MMU_Graph::Node &node_from     = this->nodes[update_queue.front()];
            update_queue.pop();
            if (node_from.arc_idxs.empty())
                continue;

            assert(node_from.arc_idxs.size() == 1);
            size_t           node_to_idx = arcs[node_from.arc_idxs.front()].to_idx;
            MMU_Graph::Node &node_to     = this->nodes[node_to_idx];
            this->remove_edge(node_from_idx, node_to_idx);
            if (node_to.arc_idxs.size() == 1 && node_to_idx >= this->all_border_points)
                update_queue.emplace(node_to_idx);
        }
    }

    void add_contours(const std::vector<std::vector<ColoredLine>> &color_poly)
    {
        this->all_border_points = nodes.size();
        this->polygon_sizes     = std::vector<size_t>(color_poly.size());
        for (size_t polygon_idx = 0; polygon_idx < color_poly.size(); ++polygon_idx)
            this->polygon_sizes[polygon_idx] = color_poly[polygon_idx].size();
        this->polygon_idx_offset    = std::vector<size_t>(color_poly.size());
        this->polygon_idx_offset[0] = 0;
        for (size_t polygon_idx = 1; polygon_idx < color_poly.size(); ++polygon_idx) {
            this->polygon_idx_offset[polygon_idx] = this->polygon_idx_offset[polygon_idx - 1] + color_poly[polygon_idx - 1].size();
        }

        size_t poly_idx = 0;
        for (const std::vector<ColoredLine> &color_lines : color_poly) {
            size_t line_idx = 0;
            for (const ColoredLine &color_line : color_lines) {
                size_t from_idx = this->get_global_index(poly_idx, line_idx);
                size_t to_idx   = this->get_global_index(poly_idx, (line_idx + 1) % color_lines.size());
                this->append_edge(from_idx, to_idx, color_line.color, ARC_TYPE::BORDER);
                ++line_idx;
            }
            ++poly_idx;
        }
    }

    // Nodes 0..all_border_points are only one with are on countour. Other vertexis are consider as not on coouter. So we check if base on attach index
    inline bool is_vertex_on_contour(const Voronoi::VD::vertex_type *vertex) const
    {
        assert(vertex != nullptr);
        return vertex->color() < this->all_border_points;
    }

    [[nodiscard]] inline bool is_edge_attach_to_contour(const voronoi_diagram<double>::const_edge_iterator &edge_iterator) const
    {
        return this->is_vertex_on_contour(edge_iterator->vertex0()) || this->is_vertex_on_contour(edge_iterator->vertex1());
    }

    [[nodiscard]] inline bool is_edge_connecting_two_contour_vertices(const voronoi_diagram<double>::const_edge_iterator &edge_iterator) const
    {
        return this->is_vertex_on_contour(edge_iterator->vertex0()) && this->is_vertex_on_contour(edge_iterator->vertex1());
    }

    // All Voronoi vertices are post-processes to merge very close vertices to single. Witch eliminates issues with intersection edges.
    // Also, Voronoi vertices outside of the bounding of input polygons are throw away by marking them.
    void append_voronoi_vertices(const Geometry::VoronoiDiagram &vd, const Polygons &color_poly_tmp, BoundingBox bbox) {
        bbox.offset(SCALED_EPSILON);

        struct CPoint
        {
            CPoint() = delete;
            CPoint(const Vec2d &point, size_t contour_idx, size_t point_idx) : m_point_double(point), m_point(mk_point(point)), m_point_idx(point_idx), m_contour_idx(contour_idx) {}
            CPoint(const Vec2d &point, size_t point_idx) : m_point_double(point), m_point(mk_point(point)), m_point_idx(point_idx), m_contour_idx(0) {}
            const Vec2d m_point_double;
            const Point m_point;
            size_t      m_point_idx;
            size_t      m_contour_idx;

            [[nodiscard]] const Vec2d &point_double() const { return m_point_double; }
            [[nodiscard]] const Point &point() const { return m_point; }
            bool operator==(const CPoint &rhs) const { return this->m_point_double == rhs.m_point_double && this->m_contour_idx == rhs.m_contour_idx && this->m_point_idx == rhs.m_point_idx; }
        };
        struct CPointAccessor { const Point* operator()(const CPoint &pt) const { return &pt.point(); }};
        typedef ClosestPointInRadiusLookup<CPoint, CPointAccessor> CPointLookupType;

        CPointLookupType closest_voronoi_point(coord_t(SCALED_EPSILON));
        CPointLookupType closest_contour_point(3 * coord_t(SCALED_EPSILON));
        for (const Polygon &polygon : color_poly_tmp)
            for (const Point &pt : polygon.points)
                closest_contour_point.insert(CPoint(Vec2d(pt.x(), pt.y()), &polygon - &color_poly_tmp.front(), &pt - &polygon.points.front()));

        for (const voronoi_diagram<double>::vertex_type &vertex : vd.vertices()) {
            vertex.color(-1);
            Vec2d vertex_point_double = Vec2d(vertex.x(), vertex.y());
            Point vertex_point        = mk_point(vertex);

            const Vec2d &first_point_double  = this->nodes[this->get_border_arc(vertex.incident_edge()->cell()->source_index()).from_idx].point;
            const Vec2d &second_point_double = this->nodes[this->get_border_arc(vertex.incident_edge()->twin()->cell()->source_index()).from_idx].point;

            if (vertex_equal_to_point(&vertex, first_point_double)) {
                assert(vertex.color() != vertex.incident_edge()->cell()->source_index());
                assert(vertex.color() != vertex.incident_edge()->twin()->cell()->source_index());
                vertex.color(this->get_border_arc(vertex.incident_edge()->cell()->source_index()).from_idx);
            } else if (vertex_equal_to_point(&vertex, second_point_double)) {
                assert(vertex.color() != vertex.incident_edge()->cell()->source_index());
                assert(vertex.color() != vertex.incident_edge()->twin()->cell()->source_index());
                vertex.color(this->get_border_arc(vertex.incident_edge()->twin()->cell()->source_index()).from_idx);
            } else if (bbox.contains(vertex_point)) {
                if (auto [contour_pt, c_dist_sqr] = closest_contour_point.find(vertex_point); contour_pt != nullptr && c_dist_sqr < Slic3r::sqr(3 * SCALED_EPSILON)) {
                    vertex.color(this->get_global_index(contour_pt->m_contour_idx, contour_pt->m_point_idx));
                } else if (auto [voronoi_pt, v_dist_sqr] = closest_voronoi_point.find(vertex_point); voronoi_pt == nullptr || v_dist_sqr >= Slic3r::sqr(SCALED_EPSILON / 10.0)) {
                    closest_voronoi_point.insert(CPoint(vertex_point_double, this->nodes_count()));
                    vertex.color(this->nodes_count());
                    this->nodes.push_back({vertex_point_double});
                } else {
                    // Boost Voronoi diagram generator sometimes creates two very closed points instead of one point.
                    // For the example points (146872.99999999997, -146872.99999999997) and (146873, -146873), this example also included in Voronoi generator test cases.
                    std::vector<std::pair<const CPoint *, double>> all_closes_c_points = closest_voronoi_point.find_all(vertex_point);
                    int                                            merge_to_point      = -1;
                    for (const std::pair<const CPoint *, double> &c_point : all_closes_c_points)
                        if ((vertex_point_double - c_point.first->point_double()).squaredNorm() <= Slic3r::sqr(EPSILON)) {
                            merge_to_point = int(c_point.first->m_point_idx);
                            break;
                        }

                    if (merge_to_point != -1) {
                        vertex.color(merge_to_point);
                    } else {
                        closest_voronoi_point.insert(CPoint(vertex_point_double, this->nodes_count()));
                        vertex.color(this->nodes_count());
                        this->nodes.push_back({vertex_point_double});
                    }
                }
            }
        }
    }

    void garbage_collect()
    {
        std::vector<int> nodes_map(this->nodes.size(), -1);
        int              nodes_count = 0;
        size_t           arcs_count  = 0;
        for (const MMU_Graph::Node &node : this->nodes)
            if (size_t node_idx = &node - &this->nodes.front(); !node.arc_idxs.empty()) {
                nodes_map[node_idx] = nodes_count++;
                arcs_count += node.arc_idxs.size();
            }

        std::vector<MMU_Graph::Node> new_nodes;
        std::vector<MMU_Graph::Arc>  new_arcs;
        new_nodes.reserve(nodes_count);
        new_arcs.reserve(arcs_count);
        for (const MMU_Graph::Node &node : this->nodes)
            if (size_t node_idx = &node - &this->nodes.front(); nodes_map[node_idx] >= 0) {
                new_nodes.push_back({node.point});
                for (const size_t &arc_idx : node.arc_idxs) {
                    const Arc &arc = this->arcs[arc_idx];
                    new_nodes.back().arc_idxs.emplace_back(new_arcs.size());
                    new_arcs.push_back({size_t(nodes_map[arc.from_idx]), size_t(nodes_map[arc.to_idx]), arc.color, arc.type});
                }
            }

        this->nodes = std::move(new_nodes);
        this->arcs  = std::move(new_arcs);
    }
};

static inline void mark_processed(const voronoi_diagram<double>::const_edge_iterator &edge_iterator)
{
    edge_iterator->color(true);
    edge_iterator->twin()->color(true);
}

// Return true, if "p" is closer to line.a, then line.b
static inline bool is_point_closer_to_beginning_of_line(const Line &line, const Point &p)
{
    return (p - line.a).cast<double>().squaredNorm() < (p - line.b).cast<double>().squaredNorm();
}

static inline bool has_same_color(const ColoredLine &cl1, const ColoredLine &cl2) { return cl1.color == cl2.color; }

// Determines if the line points from the point between two contour lines is pointing inside polygon or outside.
static inline bool points_inside(const Line &contour_first, const Line &contour_second, const Point &new_point)
{
    // Used in points_inside for decision if line leading thought the common point of two lines is pointing inside polygon or outside
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

static inline bool line_intersection_with_epsilon(const Line &line_to_extend, const Line &other, Point *intersection)
{
    Line extended_line = line_to_extend;
    extended_line.extend(15 * SCALED_EPSILON);
    return extended_line.intersection(other, intersection);
}

// For every ColoredLine in lines_colored_out, assign the index of the polygon to which belongs and also the index of this line inside of the polygon.
static inline void init_polygon_indices(const MMU_Graph                             &graph,
                                        const std::vector<std::vector<ColoredLine>> &color_poly,
                                        std::vector<ColoredLine>                    &lines_colored_out)
{
    size_t poly_idx = 0;
    for (const std::vector<ColoredLine> &color_lines : color_poly) {
        size_t line_idx = 0;
        for (size_t color_line_idx = 0; color_line_idx < color_lines.size(); ++color_line_idx) {
            size_t from_idx                            = graph.get_global_index(poly_idx, line_idx);
            lines_colored_out[from_idx].poly_idx       = int(poly_idx);
            lines_colored_out[from_idx].local_line_idx = int(line_idx);
            ++line_idx;
        }
        ++poly_idx;
    }
}

// Voronoi edges produced by Voronoi generator cloud have coordinates that don't fit inside coord_t (int32_t).
// Because of that, this function tries to clip edges that have one endpoint of the edge inside the BoundingBox.
static inline Line clip_finite_voronoi_edge(const Voronoi::VD::edge_type &edge, const BoundingBoxf &bbox)
{
    assert(edge.is_finite());
    Vec2d v0          = mk_vec2(edge.vertex0());
    Vec2d v1          = mk_vec2(edge.vertex1());
    bool  contains_v0 = bbox.contains(v0);
    bool  contains_v1 = bbox.contains(v1);
    if ((contains_v0 && contains_v1) || (!contains_v0 && !contains_v1))
        return {mk_point(edge.vertex0()), mk_point(edge.vertex1())};

    Vec2d vector = (v1 - v0).normalized() * bbox.size().norm();
    if (!contains_v0)
        v0 = (v1 - vector);
    else
        v1 = (v0 + vector);

    return {v0.cast<coord_t>(), v1.cast<coord_t>()};
}

static MMU_Graph build_graph(size_t layer_idx, const std::vector<std::vector<ColoredLine>> &color_poly)
{
    Geometry::VoronoiDiagram vd;
    std::vector<ColoredLine> lines_colored  = to_lines(color_poly);
    const Polygons           color_poly_tmp = colored_points_to_polygon(color_poly);
    const Points             points         = to_points(color_poly_tmp);
    const Lines              lines          = to_lines(color_poly_tmp);

    // The algorithm adds edges to the graph that are between two different colors.
    // If a polygon is colored entirely with one color, we need to add at least one edge from that polygon artificially.
    // Adding this edge is necessary for cases where the expolygon has an outer contour colored whole with one color
    // and a hole colored with a different color. If an edge wasn't added to the graph,
    // the entire expolygon would be colored with single random color instead of two different.
    std::vector<bool>        force_edge_adding(color_poly.size());

    // For each polygon, check if it is all colored with the same color. If it is, we need to force adding one edge to it.
    for (const std::vector<ColoredLine> &c_poly : color_poly) {
        bool force_edge = true;
        for (const ColoredLine &c_line : c_poly)
            if (c_line.color != c_poly.front().color) {
                force_edge = false;
                break;
            }
        force_edge_adding[&c_poly - &color_poly.front()] = force_edge;
    }

    boost::polygon::construct_voronoi(lines_colored.begin(), lines_colored.end(), &vd);
    MMU_Graph graph;
    graph.nodes.reserve(points.size() + vd.vertices().size());
    for (const Point &point : points)
        graph.nodes.push_back({Vec2d(double(point.x()), double(point.y()))});

    graph.add_contours(color_poly);
    init_polygon_indices(graph, color_poly, lines_colored);

    assert(graph.nodes.size() == lines_colored.size());
    BoundingBox bbox = get_extents(color_poly_tmp);
    graph.append_voronoi_vertices(vd, color_poly_tmp, bbox);

    auto get_prev_contour_line = [&lines_colored, &color_poly, &graph](const voronoi_diagram<double>::const_edge_iterator &edge_it) -> ColoredLine {
        size_t contour_line_local_idx = lines_colored[edge_it->cell()->source_index()].local_line_idx;
        size_t contour_line_size      = color_poly[lines_colored[edge_it->cell()->source_index()].poly_idx].size();
        size_t contour_prev_idx       = graph.get_global_index(lines_colored[edge_it->cell()->source_index()].poly_idx,
                                                         (contour_line_local_idx > 0) ? contour_line_local_idx - 1 : contour_line_size - 1);
        return lines_colored[contour_prev_idx];
    };

    auto get_next_contour_line = [&lines_colored, &color_poly, &graph](const voronoi_diagram<double>::const_edge_iterator &edge_it) -> ColoredLine {
        size_t contour_line_local_idx = lines_colored[edge_it->cell()->source_index()].local_line_idx;
        size_t contour_line_size      = color_poly[lines_colored[edge_it->cell()->source_index()].poly_idx].size();
        size_t contour_next_idx       = graph.get_global_index(lines_colored[edge_it->cell()->source_index()].poly_idx,
                                                         (contour_line_local_idx + 1) % contour_line_size);
        return lines_colored[contour_next_idx];
    };

    bbox.offset(scale_(10.));
    const BoundingBoxf bbox_clip(bbox.min.cast<double>(), bbox.max.cast<double>());
    const double       bbox_dim_max = double(std::max(bbox.size().x(), bbox.size().y()));

    // Make a copy of the input segments with the double type.
    std::vector<Voronoi::Internal::segment_type> segments;
    for (const Line &line : lines)
        segments.emplace_back(Voronoi::Internal::point_type(double(line.a(0)), double(line.a(1))),
                              Voronoi::Internal::point_type(double(line.b(0)), double(line.b(1))));

    for (auto edge_it = vd.edges().begin(); edge_it != vd.edges().end(); ++edge_it) {
        // Skip second half-edge
        if (edge_it->cell()->source_index() > edge_it->twin()->cell()->source_index() || edge_it->color())
            continue;

        if (edge_it->is_infinite() && (edge_it->vertex0() != nullptr || edge_it->vertex1() != nullptr)) {
            // Infinite edge is leading through a point on the counter, but there are no Voronoi vertices.
            // So we could fix this case by computing the intersection between the contour line and infinity edge.
            std::vector<Voronoi::Internal::point_type> samples;
            Voronoi::Internal::clip_infinite_edge(points, segments, *edge_it, bbox_dim_max, &samples);
            if (samples.empty())
                continue;

            const Line         edge_line(mk_point(samples[0]), mk_point(samples[1]));
            const ColoredLine &contour_line = lines_colored[edge_it->cell()->source_index()];
            Point              contour_intersection;

            if (line_intersection_with_epsilon(contour_line.line, edge_line, &contour_intersection)) {
                const MMU_Graph::Arc &graph_arc = graph.get_border_arc(edge_it->cell()->source_index());
                const size_t          from_idx  = (edge_it->vertex1() != nullptr) ? edge_it->vertex1()->color() : edge_it->vertex0()->color();
                size_t                to_idx    = ((contour_line.line.a - contour_intersection).cast<double>().squaredNorm() <
                                 (contour_line.line.b - contour_intersection).cast<double>().squaredNorm()) ?
                                                      graph_arc.from_idx :
                                                      graph_arc.to_idx;
                if (from_idx != to_idx && from_idx < graph.nodes_count() && to_idx < graph.nodes_count()) {
                    graph.append_edge(from_idx, to_idx);
                    mark_processed(edge_it);
                }
            }
        } else if (edge_it->is_finite()) {
            // Both points are on contour, so skip them. In cases of duplicate Voronoi vertices, skip edges between the same two points.
            if (graph.is_edge_connecting_two_contour_vertices(edge_it) || (edge_it->vertex0()->color() == edge_it->vertex1()->color()))
                continue;

            const Line        edge_line         = clip_finite_voronoi_edge(*edge_it, bbox_clip);
            const Line        contour_line      = lines_colored[edge_it->cell()->source_index()].line;
            const ColoredLine colored_line      = lines_colored[edge_it->cell()->source_index()];
            const ColoredLine contour_line_prev = get_prev_contour_line(edge_it);
            const ColoredLine contour_line_next = get_next_contour_line(edge_it);

            if (edge_it->vertex0()->color() >= graph.nodes_count() || edge_it->vertex1()->color() >= graph.nodes_count()) {
                enum class Vertex { VERTEX0, VERTEX1 };
                auto append_edge_if_intersects_with_contour = [&graph, &lines_colored, &edge_line, &contour_line](const voronoi_diagram<double>::const_edge_iterator &edge_iterator, const Vertex vertex) {
                    Point intersection;
                    Line  contour_line_twin = lines_colored[edge_iterator->twin()->cell()->source_index()].line;
                    if (line_intersection_with_epsilon(contour_line_twin, edge_line, &intersection)) {
                        const MMU_Graph::Arc &graph_arc = graph.get_border_arc(edge_iterator->twin()->cell()->source_index());
                        const size_t          to_idx_l  = is_point_closer_to_beginning_of_line(contour_line_twin, intersection) ? graph_arc.from_idx :
                                                                                                                                  graph_arc.to_idx;
                        graph.append_edge(vertex == Vertex::VERTEX0 ? edge_iterator->vertex0()->color() : edge_iterator->vertex1()->color(), to_idx_l);
                    } else if (line_intersection_with_epsilon(contour_line, edge_line, &intersection)) {
                        const MMU_Graph::Arc &graph_arc = graph.get_border_arc(edge_iterator->cell()->source_index());
                        const size_t to_idx_l = is_point_closer_to_beginning_of_line(contour_line, intersection) ? graph_arc.from_idx : graph_arc.to_idx;
                        graph.append_edge(vertex == Vertex::VERTEX0 ? edge_iterator->vertex0()->color() : edge_iterator->vertex1()->color(), to_idx_l);
                    }
                    mark_processed(edge_iterator);
                };

                if (edge_it->vertex0()->color() < graph.nodes_count() && !graph.is_vertex_on_contour(edge_it->vertex0()))
                    append_edge_if_intersects_with_contour(edge_it, Vertex::VERTEX0);

                if (edge_it->vertex1()->color() < graph.nodes_count() && !graph.is_vertex_on_contour(edge_it->vertex1()))
                    append_edge_if_intersects_with_contour(edge_it, Vertex::VERTEX1);
            } else if (graph.is_edge_attach_to_contour(edge_it)) {
                mark_processed(edge_it);
                // Skip edges witch connection two points on a contour
                if (graph.is_edge_connecting_two_contour_vertices(edge_it))
                    continue;

                const size_t from_idx = edge_it->vertex0()->color();
                const size_t to_idx   = edge_it->vertex1()->color();
                if (graph.is_vertex_on_contour(edge_it->vertex0())) {
                    if (is_point_closer_to_beginning_of_line(contour_line, edge_line.a)) {
                        if ((!has_same_color(contour_line_prev, colored_line) || force_edge_adding[colored_line.poly_idx]) && points_inside(contour_line_prev.line, contour_line, edge_line.b)) {
                            graph.append_edge(from_idx, to_idx);
                            force_edge_adding[colored_line.poly_idx] = false;
                        }
                    } else {
                        if ((!has_same_color(contour_line_next, colored_line) || force_edge_adding[colored_line.poly_idx]) && points_inside(contour_line, contour_line_next.line, edge_line.b)) {
                            graph.append_edge(from_idx, to_idx);
                            force_edge_adding[colored_line.poly_idx] = false;
                        }
                    }
                } else {
                    assert(graph.is_vertex_on_contour(edge_it->vertex1()));
                    if (is_point_closer_to_beginning_of_line(contour_line, edge_line.b)) {
                        if ((!has_same_color(contour_line_prev, colored_line) || force_edge_adding[colored_line.poly_idx]) && points_inside(contour_line_prev.line, contour_line, edge_line.a)) {
                            graph.append_edge(from_idx, to_idx);
                            force_edge_adding[colored_line.poly_idx] = false;
                        }
                    } else {
                        if ((!has_same_color(contour_line_next, colored_line) || force_edge_adding[colored_line.poly_idx]) && points_inside(contour_line, contour_line_next.line, edge_line.a)) {
                            graph.append_edge(from_idx, to_idx);
                            force_edge_adding[colored_line.poly_idx] = false;
                        }
                    }
                }
            } else if (Point intersection; line_intersection_with_epsilon(contour_line, edge_line, &intersection)) {
                mark_processed(edge_it);
                Vec2d real_v0_double = graph.nodes[edge_it->vertex0()->color()].point;
                Vec2d real_v1_double = graph.nodes[edge_it->vertex1()->color()].point;
                Point real_v0        = Point(coord_t(real_v0_double.x()), coord_t(real_v0_double.y()));
                Point real_v1        = Point(coord_t(real_v1_double.x()), coord_t(real_v1_double.y()));

                if (is_point_closer_to_beginning_of_line(contour_line, intersection)) {
                    Line first_part(intersection, real_v0);
                    Line second_part(intersection, real_v1);

                    if (!has_same_color(contour_line_prev, colored_line)) {
                        if (points_inside(contour_line_prev.line, contour_line, first_part.b))
                            graph.append_edge(edge_it->vertex0()->color(), graph.get_border_arc(edge_it->cell()->source_index()).from_idx);

                        if (points_inside(contour_line_prev.line, contour_line, second_part.b))
                            graph.append_edge(edge_it->vertex1()->color(), graph.get_border_arc(edge_it->cell()->source_index()).from_idx);
                    }
                } else {
                    const size_t int_point_idx    = graph.get_border_arc(edge_it->cell()->source_index()).to_idx;
                    const Vec2d  int_point_double = graph.nodes[int_point_idx].point;
                    const Point  int_point        = Point(coord_t(int_point_double.x()), coord_t(int_point_double.y()));

                    const Line first_part(int_point, real_v0);
                    const Line second_part(int_point, real_v1);

                    if (!has_same_color(contour_line_next, colored_line)) {
                        if (points_inside(contour_line, contour_line_next.line, first_part.b))
                            graph.append_edge(edge_it->vertex0()->color(), int_point_idx);

                        if (points_inside(contour_line, contour_line_next.line, second_part.b))
                            graph.append_edge(edge_it->vertex1()->color(), int_point_idx);
                    }
                }
            }
        }
    }

    for (auto edge_it = vd.edges().begin(); edge_it != vd.edges().end(); ++edge_it) {
        // Skip second half-edge and processed edges
        if (edge_it->cell()->source_index() > edge_it->twin()->cell()->source_index() || edge_it->color())
            continue;

        if (edge_it->is_finite() && !bool(edge_it->color()) && edge_it->vertex0()->color() < graph.nodes_count() &&
            edge_it->vertex1()->color() < graph.nodes_count()) {
            // Skip cases, when the edge is between two same vertices, which is in cases two near vertices were merged together.
            if (edge_it->vertex0()->color() == edge_it->vertex1()->color())
                continue;

            size_t from_idx = edge_it->vertex0()->color();
            size_t to_idx   = edge_it->vertex1()->color();
            graph.append_edge(from_idx, to_idx);
        }
        mark_processed(edge_it);
    }

    graph.remove_nodes_with_one_arc();
    return graph;
}

static inline Polygon to_polygon(const std::vector<Linef> &lines)
{
    Polygon poly_out;
    poly_out.points.reserve(lines.size());
    for (const Linef &line : lines)
        poly_out.points.emplace_back(mk_point(line.a));
    return poly_out;
}

// Returns list of polygons and assigned colors.
// It iterates through all nodes on the border between two different colors, and from this point,
// start selection always left most edges for every node to construct CCW polygons.
// Assumes that graph is planar (without self-intersection edges)
static std::vector<std::pair<Polygon, size_t>> extract_colored_segments(const MMU_Graph &graph)
{
    std::vector<bool> used_arcs(graph.arcs.size(), false);
    // When there is no next arc, then is returned original_arc or edge with is marked as used
    auto get_next = [&graph, &used_arcs](const Linef &process_line, const MMU_Graph::Arc &original_arc) -> const MMU_Graph::Arc & {
        std::vector<std::pair<const MMU_Graph::Arc *, double>> sorted_arcs;
        for (const size_t &arc_idx : graph.nodes[original_arc.to_idx].arc_idxs) {
            const MMU_Graph::Arc &arc = graph.arcs[arc_idx];
            if (graph.nodes[arc.to_idx].point == process_line.a || used_arcs[arc_idx])
                continue;

            assert(original_arc.to_idx == arc.from_idx);
            Vec2d process_line_vec_n   = (process_line.a - process_line.b).normalized();
            Vec2d neighbour_line_vec_n = (graph.nodes[arc.to_idx].point - graph.nodes[arc.from_idx].point).normalized();

            double angle = ::acos(std::clamp(neighbour_line_vec_n.dot(process_line_vec_n), -1.0, 1.0));
            if (Slic3r::cross2(neighbour_line_vec_n, process_line_vec_n) < 0.0)
                angle = 2.0 * (double) PI - angle;

            sorted_arcs.emplace_back(&arc, angle);
        }

        std::sort(sorted_arcs.begin(), sorted_arcs.end(),
                  [](std::pair<const MMU_Graph::Arc *, double> &l, std::pair<const MMU_Graph::Arc *, double> &r) -> bool { return l.second < r.second; });

        // Try to return left most edge witch is unused
        for (auto &sorted_arc : sorted_arcs)
            if (size_t arc_idx = sorted_arc.first - &graph.arcs.front(); !used_arcs[arc_idx])
                return *sorted_arc.first;

        if (sorted_arcs.empty())
            return original_arc;

        return *(sorted_arcs.front().first);
    };

    auto all_arc_used = [&used_arcs](const MMU_Graph::Node &node) -> bool {
        return std::all_of(node.arc_idxs.cbegin(), node.arc_idxs.cend(), [&used_arcs](const size_t &arc_idx) -> bool { return used_arcs[arc_idx]; });
    };

    std::vector<std::pair<Polygon, size_t>> polygons_segments;
    for (size_t node_idx = 0; node_idx < graph.all_border_points; ++node_idx) {
        const MMU_Graph::Node &node = graph.nodes[node_idx];

        for (const size_t &arc_idx : node.arc_idxs) {
            const MMU_Graph::Arc &arc = graph.arcs[arc_idx];
            if (arc.type == MMU_Graph::ARC_TYPE::NON_BORDER || used_arcs[arc_idx])
                continue;

            Linef process_line(node.point, graph.nodes[arc.to_idx].point);
            used_arcs[arc_idx] = true;

            std::vector<Linef> face_lines;
            face_lines.emplace_back(process_line);
            Vec2d start_p = process_line.a;

            Linef                 p_vec = process_line;
            const MMU_Graph::Arc *p_arc = &arc;
            do {
                const MMU_Graph::Arc &next         = get_next(p_vec, *p_arc);
                size_t                next_arc_idx = &next - &graph.arcs.front();
                face_lines.emplace_back(graph.nodes[next.from_idx].point, graph.nodes[next.to_idx].point);
                if (used_arcs[next_arc_idx])
                    break;

                used_arcs[next_arc_idx] = true;
                p_vec                   = Linef(graph.nodes[next.from_idx].point, graph.nodes[next.to_idx].point);
                p_arc                   = &next;
            } while (graph.nodes[p_arc->to_idx].point != start_p || !all_arc_used(graph.nodes[p_arc->to_idx]));

            Polygon poly = to_polygon(face_lines);
            if (poly.is_counter_clockwise() && poly.is_valid())
                polygons_segments.emplace_back(poly, arc.color);
        }
    }
    return polygons_segments;
}

// Used in remove_multiple_edges_in_vertices()
// Returns length of edge with is connected to contour. To this length is include other edges with follows it if they are almost straight (with the
// tolerance of 15) And also if node between two subsequent edges is connected only to these two edges.
static inline double compute_edge_length(const MMU_Graph &graph, const size_t start_idx, const size_t &start_arc_idx)
{
    assert(start_arc_idx < graph.arcs.size());
    std::vector<bool> used_arcs(graph.arcs.size(), false);

    used_arcs[start_arc_idx]                = true;
    const MMU_Graph::Arc *arc               = &graph.arcs[start_arc_idx];
    size_t                idx               = start_idx;
    double                line_total_length = (graph.nodes[arc->to_idx].point - graph.nodes[idx].point).norm();;
    while (graph.nodes[arc->to_idx].arc_idxs.size() == 2) {
        bool found = false;
        for (const size_t &arc_idx : graph.nodes[arc->to_idx].arc_idxs) {
            if (const MMU_Graph::Arc &arc_n = graph.arcs[arc_idx]; arc_n.type == MMU_Graph::ARC_TYPE::NON_BORDER && !used_arcs[arc_idx] && arc_n.to_idx != idx) {
                Linef first_line(graph.nodes[idx].point, graph.nodes[arc->to_idx].point);
                Linef second_line(graph.nodes[arc->to_idx].point, graph.nodes[arc_n.to_idx].point);

                Vec2d  first_line_vec    = (first_line.a - first_line.b);
                Vec2d  second_line_vec   = (second_line.b - second_line.a);
                Vec2d  first_line_vec_n  = first_line_vec.normalized();
                Vec2d  second_line_vec_n = second_line_vec.normalized();
                double angle             = ::acos(std::clamp(first_line_vec_n.dot(second_line_vec_n), -1.0, 1.0));
                if (Slic3r::cross2(first_line_vec_n, second_line_vec_n) < 0.0)
                    angle = 2.0 * (double) PI - angle;

                if (std::abs(angle - PI) >= (PI / 12))
                    continue;

                idx = arc->to_idx;
                arc = &arc_n;

                line_total_length += (graph.nodes[arc->to_idx].point - graph.nodes[idx].point).norm();
                used_arcs[arc_idx] = true;
                found      = true;
                break;
            }
        }
        if (!found)
            break;
    }

    return line_total_length;
}

// Used for fixing double Voronoi edges for concave parts of the polygon.
static void remove_multiple_edges_in_vertices(MMU_Graph &graph, const std::vector<std::vector<ColoredLine>> &color_poly)
{
    std::vector<std::vector<std::pair<size_t, size_t>>> colored_segments = get_all_segments(color_poly);
    for (const std::vector<std::pair<size_t, size_t>> &colored_segment_p : colored_segments) {
        size_t poly_idx = &colored_segment_p - &colored_segments.front();
        for (const std::pair<size_t, size_t> &colored_segment : colored_segment_p) {
            size_t first_idx  = graph.get_global_index(poly_idx, colored_segment.first);
            size_t second_idx = graph.get_global_index(poly_idx, (colored_segment.second + 1) % graph.polygon_sizes[poly_idx]);
            Linef  seg_line(graph.nodes[first_idx].point, graph.nodes[second_idx].point);

            if (graph.nodes[first_idx].arc_idxs.size() >= 3) {
                std::vector<std::pair<MMU_Graph::Arc *, double>> arc_to_check;
                for (const size_t &arc_idx : graph.nodes[first_idx].arc_idxs) {
                    MMU_Graph::Arc &n_arc = graph.arcs[arc_idx];
                    if (n_arc.type == MMU_Graph::ARC_TYPE::NON_BORDER) {
                        double total_len = compute_edge_length(graph, first_idx, arc_idx);
                        arc_to_check.emplace_back(&n_arc, total_len);
                    }
                }
                std::sort(arc_to_check.begin(), arc_to_check.end(),
                          [](std::pair<MMU_Graph::Arc *, double> &l, std::pair<MMU_Graph::Arc *, double> &r) -> bool { return l.second > r.second; });

                while (arc_to_check.size() > 1) {
                    graph.remove_edge(first_idx, arc_to_check.back().first->to_idx);
                    arc_to_check.pop_back();
                }
            }
        }
    }
}

static void cut_segmented_layers(const std::vector<ExPolygons>                          &input_expolygons,
                                 std::vector<std::vector<std::pair<ExPolygon, size_t>>> &segmented_regions,
                                 const float                                             cut_width,
                                 const std::function<void()>                            &throw_on_cancel_callback)
{
    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - cutting segmented layers in parallel - begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, segmented_regions.size()),[&segmented_regions, &input_expolygons, &cut_width, &throw_on_cancel_callback](const tbb::blocked_range<size_t>& range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            std::vector<std::pair<ExPolygon, size_t>> segmented_regions_cuts;
            for (const std::pair<ExPolygon, size_t> &colored_expoly : segmented_regions[layer_idx]) {
                ExPolygons cut_colored_expoly = diff_ex(colored_expoly.first, offset_ex(input_expolygons[layer_idx], cut_width));
                for (ExPolygon &expoly : cut_colored_expoly)
                    segmented_regions_cuts.emplace_back(std::move(expoly), colored_expoly.second);
            }
            segmented_regions[layer_idx] = std::move(segmented_regions_cuts);
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - cutting segmented layers in parallel - end";
}

//#define MMU_SEGMENTATION_DEBUG_TOP_BOTTOM

// Returns MMU segmentation of top and bottom layers based on painting in MMU segmentation gizmo
static inline std::vector<std::vector<ExPolygons>> mmu_segmentation_top_and_bottom_layers(const PrintObject             &print_object,
                                                                                          const std::vector<ExPolygons> &input_expolygons,
                                                                                          const std::function<void()>   &throw_on_cancel_callback)
{
    const size_t num_extruders = print_object.print()->config().nozzle_diameter.size() + 1;
    const size_t num_layers    = input_expolygons.size();
    const ConstLayerPtrsAdaptor layers = print_object.layers();

    // Maximum number of top / bottom layers accounts for maximum overlap of one thread group into a neighbor thread group.
    int max_top_layers = 0;
    int max_bottom_layers = 0;
    int granularity = 1;
    for (size_t i = 0; i < print_object.num_printing_regions(); ++ i) {
        const PrintRegionConfig &config = print_object.printing_region(i).config();
        max_top_layers    = std::max(max_top_layers, config.top_solid_layers.value);
        max_bottom_layers = std::max(max_bottom_layers, config.bottom_solid_layers.value);
        granularity       = std::max(granularity, std::max(config.top_solid_layers.value, config.bottom_solid_layers.value) - 1);
    }

    // Project upwards pointing painted triangles over top surfaces,
    // project downards pointing painted triangles over bottom surfaces.
    std::vector<std::vector<Polygons>> top_raw(num_extruders), bottom_raw(num_extruders);
    std::vector<float> zs = zs_from_layers(print_object.layers());
    Transform3d        object_trafo = print_object.trafo_centered();

#ifdef MMU_SEGMENTATION_DEBUG_TOP_BOTTOM
    static int iRun = 0;
#endif // NDEBUG

    if (max_top_layers > 0 || max_bottom_layers > 0) {
        for (const ModelVolume *mv : print_object.model_object()->volumes)
            if (mv->is_model_part()) {
                const Transform3d volume_trafo = object_trafo * mv->get_matrix();
                for (size_t extruder_idx = 0; extruder_idx < num_extruders; ++ extruder_idx) {
                    const indexed_triangle_set painted = mv->mmu_segmentation_facets.get_facets_strict(*mv, EnforcerBlockerType(extruder_idx));
#ifdef MMU_SEGMENTATION_DEBUG_TOP_BOTTOM
                    {
                        static int iRun = 0;
                        its_write_obj(painted, debug_out_path("mm-painted-patch-%d-%d.obj", iRun ++, extruder_idx).c_str());
                    }
#endif // MMU_SEGMENTATION_DEBUG_TOP_BOTTOM
                    if (! painted.indices.empty()) {
                        std::vector<Polygons> top, bottom;
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

#ifdef MMU_SEGMENTATION_DEBUG_TOP_BOTTOM
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
#endif // MMU_SEGMENTATION_DEBUG_TOP_BOTTOM

    std::vector<std::vector<ExPolygons>> triangles_by_color_bottom(num_extruders);
    std::vector<std::vector<ExPolygons>> triangles_by_color_top(num_extruders);
    triangles_by_color_bottom.assign(num_extruders, std::vector<ExPolygons>(num_layers * 2));
    triangles_by_color_top.assign(num_extruders, std::vector<ExPolygons>(num_layers * 2));

    struct LayerColorStat {
        // Number of regions for a queried color.
        int     num_regions             { 0 };
        // Maximum perimeter extrusion width for a queried color.
        float   extrusion_width         { 0.f };
        // Minimum radius of a region to be printable. Used to filter regions by morphological opening.
        float   small_region_threshold  { 0.f };
        // Maximum number of top layers for a queried color.
        int     top_solid_layers        { 0 };
        // Maximum number of bottom layers for a queried color.
        int     bottom_solid_layers     { 0 };
    };
    auto layer_color_stat = [&layers = std::as_const(layers)](const size_t layer_idx, const size_t color_idx) -> LayerColorStat {
        LayerColorStat out;
        const Layer &layer = *layers[layer_idx];
        for (const LayerRegion *region : layer.regions())
            if (const PrintRegionConfig &config = region->region().config();
                // color_idx == 0 means "don't know" extruder aka the underlying extruder.
                // As this region may split existing regions, we collect statistics over all regions for color_idx == 0.
                color_idx == 0 || config.perimeter_extruder == int(color_idx)) {
                out.extrusion_width     = std::max<float>(out.extrusion_width, float(config.perimeter_extrusion_width));
                out.top_solid_layers    = std::max<int>(out.top_solid_layers, config.top_solid_layers);
                out.bottom_solid_layers = std::max<int>(out.bottom_solid_layers, config.bottom_solid_layers);
                out.small_region_threshold = config.gap_fill_enabled.value && config.gap_fill_speed.value > 0 ?
                                             // Gap fill enabled. Enable a single line of 1/2 extrusion width.
                                             0.5f * float(config.perimeter_extrusion_width) :
                                             // Gap fill disabled. Enable two lines slightly overlapping.
                                             float(config.perimeter_extrusion_width) + 0.7f * Flow::rounded_rectangle_extrusion_spacing(float(config.perimeter_extrusion_width), float(layer.height));
                out.small_region_threshold = scaled<float>(out.small_region_threshold * 0.5f);
                ++ out.num_regions;
            }
        assert(out.num_regions > 0);
        out.extrusion_width = scaled<float>(out.extrusion_width);
        return out;
    };

    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers, granularity), [&granularity, &num_layers, &num_extruders, &layer_color_stat, &top_raw, &triangles_by_color_top,
                                                                               &throw_on_cancel_callback, &input_expolygons, &bottom_raw, &triangles_by_color_bottom](const tbb::blocked_range<size_t> &range) {
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
                            for (int last_idx = int(layer_idx) - 1; last_idx >= std::max(int(layer_idx - stat.top_solid_layers), int(0)); --last_idx) {
                                offset -= stat.extrusion_width;
                                layer_slices_trimmed = intersection_ex(layer_slices_trimmed, input_expolygons[last_idx]);
                                ExPolygons last = opening_ex(intersection_ex(top_ex, offset_ex(layer_slices_trimmed, offset)), stat.small_region_threshold);
                                if (last.empty())
                                    break;
                                append(triangles_by_color_top[color_idx][last_idx + layer_idx_offset], std::move(last));
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
                            for (size_t last_idx = layer_idx + 1; last_idx < std::min(layer_idx + stat.bottom_solid_layers, num_layers); ++last_idx) {
                                offset -= stat.extrusion_width;
                                layer_slices_trimmed = intersection_ex(layer_slices_trimmed, input_expolygons[last_idx]);
                                ExPolygons last = opening_ex(intersection_ex(bottom_ex, offset_ex(layer_slices_trimmed, offset)), stat.small_region_threshold);
                                if (last.empty())
                                    break;
                                append(triangles_by_color_bottom[color_idx][last_idx + layer_idx_offset], std::move(last));
                            }
                        }
                    }
            }
        }
    });

    std::vector<std::vector<ExPolygons>> triangles_by_color_merged(num_extruders);
    triangles_by_color_merged.assign(num_extruders, std::vector<ExPolygons>(num_layers));
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&triangles_by_color_merged, &triangles_by_color_bottom, &triangles_by_color_top, &num_layers, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
            throw_on_cancel_callback();
            for (size_t color_idx = 0; color_idx < triangles_by_color_merged.size(); ++color_idx) {
                auto &self = triangles_by_color_merged[color_idx][layer_idx];
                append(self, std::move(triangles_by_color_bottom[color_idx][layer_idx]));
                append(self, std::move(triangles_by_color_bottom[color_idx][layer_idx + num_layers]));
                append(self, std::move(triangles_by_color_top[color_idx][layer_idx]));
                append(self, std::move(triangles_by_color_top[color_idx][layer_idx + num_layers]));
                self = union_ex(self);
            }
            // Trim one region by the other if some of the regions overlap.
            for (size_t color_idx = 1; color_idx < triangles_by_color_merged.size(); ++ color_idx)
                triangles_by_color_merged[color_idx][layer_idx] = diff_ex(triangles_by_color_merged[color_idx][layer_idx],
                                                                          triangles_by_color_merged[color_idx - 1][layer_idx]);
        }
    });

    return triangles_by_color_merged;
}

static std::vector<std::vector<std::pair<ExPolygon, size_t>>> merge_segmented_layers(
    const std::vector<std::vector<std::pair<ExPolygon, size_t>>> &segmented_regions,
    std::vector<std::vector<ExPolygons>>                        &&top_and_bottom_layers,
    const std::function<void()>                                  &throw_on_cancel_callback)
{
    std::vector<std::vector<std::pair<ExPolygon, size_t>>> segmented_regions_merged(segmented_regions.size());

    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - merging segmented layers in parallel - begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, segmented_regions.size()), [&segmented_regions, &top_and_bottom_layers, &segmented_regions_merged, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            for (const std::pair<ExPolygon, size_t> &colored_expoly : segmented_regions[layer_idx]) {
                throw_on_cancel_callback();
                // Zero is the default color of the volume.
                if(colored_expoly.second == 0)
                    continue;
                ExPolygons cut_colored_expoly = {colored_expoly.first};
                for (const std::vector<ExPolygons> &top_and_bottom_layer : top_and_bottom_layers)
                    cut_colored_expoly = diff_ex(cut_colored_expoly, top_and_bottom_layer[layer_idx]);
                for (ExPolygon &ex_poly : cut_colored_expoly)
                    segmented_regions_merged[layer_idx].emplace_back(std::move(ex_poly), colored_expoly.second - 1);
            }

            for (size_t color_idx = 1; color_idx < top_and_bottom_layers.size(); ++color_idx)
                for (ExPolygon &expoly : top_and_bottom_layers[color_idx][layer_idx])
                    segmented_regions_merged[layer_idx].emplace_back(std::move(expoly), color_idx - 1);
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - merging segmented layers in parallel - end";

    return segmented_regions_merged;
}

#ifdef MMU_SEGMENTATION_DEBUG_REGIONS
static void export_regions_to_svg(const std::string &path, const std::vector<std::pair<ExPolygon, size_t>> &regions, const ExPolygons &lslices)
{
    const std::vector<std::string> colors       = {"blue", "cyan", "red", "orange", "magenta", "pink", "purple", "yellow"};
    coordf_t                       stroke_width = scale_(0.05);
    BoundingBox                    bbox         = get_extents(lslices);
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    svg.draw_outline(lslices, "green", "lime", stroke_width);
    for (const std::pair<ExPolygon, size_t> &region : regions) {
        int region_color = int(region.second);
        if (region_color >= 0 && region_color < int(colors.size()))
            svg.draw(region.first, colors[region_color]);
        else
            svg.draw(region.first, "black");
    }
}
#endif // MMU_SEGMENTATION_DEBUG_REGIONS

#ifdef MMU_SEGMENTATION_DEBUG_GRAPH
static void export_graph_to_svg(const std::string &path, const MMU_Graph &graph, const ExPolygons &lslices)
{
    const std::vector<std::string> colors       = {"blue", "cyan", "red", "orange", "magenta", "pink", "purple", "green", "yellow"};
    coordf_t                       stroke_width = scale_(0.05);
    BoundingBox                    bbox         = get_extents(lslices);
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);
    for (const MMU_Graph::Node &node : graph.nodes)
        for (const size_t &arc_idx : node.arc_idxs) {
            const MMU_Graph::Arc &arc = graph.arcs[arc_idx];
            Line arc_line(mk_point(node.point), mk_point(graph.nodes[arc.to_idx].point));
            if (arc.type == MMU_Graph::ARC_TYPE::BORDER && arc.color >= 0 && arc.color < int(colors.size()))
                svg.draw(arc_line, colors[arc.color], stroke_width);
            else
                svg.draw(arc_line, "black", stroke_width);
        }
}
#endif // MMU_SEGMENTATION_DEBUG_GRAPH

#ifdef MMU_SEGMENTATION_DEBUG_INPUT
void export_processed_input_expolygons_to_svg(const std::string &path, const LayerRegionPtrs &regions, const ExPolygons &processed_input_expolygons)
{
    coordf_t    stroke_width = scale_(0.05);
    BoundingBox bbox         = get_extents(regions);
    bbox.merge(get_extents(processed_input_expolygons));
    bbox.offset(scale_(1.));
    ::Slic3r::SVG svg(path.c_str(), bbox);

    for (LayerRegion *region : regions)
        svg.draw_outline(region->slices.surfaces, "blue", "cyan", stroke_width);

    svg.draw_outline(processed_input_expolygons, "red", "pink", stroke_width);
}
#endif // MMU_SEGMENTATION_DEBUG_INPUT

// Check if all ColoredLine representing a single layer uses the same color.
static bool has_layer_only_one_color(const std::vector<std::vector<ColoredLine>> &colored_polygons)
{
    assert(!colored_polygons.empty());
    assert(!colored_polygons.front().empty());
    int first_line_color = colored_polygons.front().front().color;
    for (const std::vector<ColoredLine> &colored_polygon : colored_polygons)
        for (const ColoredLine &colored_line : colored_polygon)
            if (first_line_color != colored_line.color)
                return false;

    return true;
}

std::vector<std::vector<std::pair<ExPolygon, size_t>>> multi_material_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback)
{
    std::vector<std::vector<std::pair<ExPolygon, size_t>>> segmented_regions(print_object.layers().size());
    std::vector<std::vector<PaintedLine>>                  painted_lines(print_object.layers().size());
    std::array<std::mutex, 64>                             painted_lines_mutex;
    std::vector<EdgeGrid::Grid>                            edge_grids(print_object.layers().size());
    const ConstLayerPtrsAdaptor                            layers = print_object.layers();
    std::vector<ExPolygons>                                input_expolygons(layers.size());

    throw_on_cancel_callback();

    // Merge all regions and remove small holes
    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - slices preparation in parallel - begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&layers, &input_expolygons, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
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
            input_expolygons[layer_idx] = smooth_outward(expolygons_simplify(offset_ex(ex_polygons, -10.f * float(SCALED_EPSILON)), 5 * SCALED_EPSILON), 10 * coord_t(SCALED_EPSILON));

#ifdef MMU_SEGMENTATION_DEBUG_INPUT
            {
                static int iRun = 0;
                export_processed_input_expolygons_to_svg(debug_out_path("mm-input-%d-%d.svg", layer_idx, iRun++), layers[layer_idx]->regions(), input_expolygons[layer_idx]);
            }
#endif // MMU_SEGMENTATION_DEBUG_INPUT
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - slices preparation in parallel - end";

    for (size_t layer_idx = 0; layer_idx < layers.size(); ++layer_idx) {
        throw_on_cancel_callback();
        BoundingBox bbox(get_extents(layers[layer_idx]->regions()));
        bbox.merge(get_extents(input_expolygons[layer_idx]));
        // Projected triangles may slightly exceed the input polygons.
        bbox.offset(20 * SCALED_EPSILON);
        edge_grids[layer_idx].set_bbox(bbox);
        edge_grids[layer_idx].create(input_expolygons[layer_idx], coord_t(scale_(10.)));
    }

    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - projection of painted triangles - begin";
    for (const ModelVolume *mv : print_object.model_object()->volumes) {
        const size_t num_extruders = print_object.print()->config().nozzle_diameter.size() + 1;
        tbb::parallel_for(tbb::blocked_range<size_t>(1, num_extruders), [&mv, &print_object, &edge_grids, &painted_lines, &painted_lines_mutex, &input_expolygons, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
            for (size_t extruder_idx = range.begin(); extruder_idx < range.end(); ++extruder_idx) {
                throw_on_cancel_callback();
                const indexed_triangle_set custom_facets = mv->mmu_segmentation_facets.get_facets(*mv, EnforcerBlockerType(extruder_idx));
                if (!mv->is_model_part() || custom_facets.indices.empty())
                    continue;

                const Transform3f tr = print_object.trafo().cast<float>() * mv->get_matrix().cast<float>();
                tbb::parallel_for(tbb::blocked_range<size_t>(0, custom_facets.indices.size()), [&tr, &custom_facets, &print_object, &edge_grids, &input_expolygons, &painted_lines, &painted_lines_mutex, &extruder_idx](const tbb::blocked_range<size_t> &range) {
                    for (size_t facet_idx = range.begin(); facet_idx < range.end(); ++facet_idx) {
                        float min_z = std::numeric_limits<float>::max();
                        float max_z = std::numeric_limits<float>::lowest();

                        std::array<Vec3f, 3> facet;
                        for (int p_idx = 0; p_idx < 3; ++p_idx) {
                            facet[p_idx] = tr * custom_facets.vertices[custom_facets.indices[facet_idx](p_idx)];
                            max_z        = std::max(max_z, facet[p_idx].z());
                            min_z        = std::min(min_z, facet[p_idx].z());
                        }

                        // Sort the vertices by z-axis for simplification of projected_facet on slices
                        std::sort(facet.begin(), facet.end(), [](const Vec3f &p1, const Vec3f &p2) { return p1.z() < p2.z(); });

                        // Find lowest slice not below the triangle.
                        auto first_layer = std::upper_bound(print_object.layers().begin(), print_object.layers().end(), float(min_z - EPSILON),
                                                            [](float z, const Layer *l1) { return z < l1->slice_z; });
                        auto last_layer  = std::upper_bound(print_object.layers().begin(), print_object.layers().end(), float(max_z + EPSILON),
                                                           [](float z, const Layer *l1) { return z < l1->slice_z; });
                        --last_layer;

                        for (auto layer_it = first_layer; layer_it != (last_layer + 1); ++layer_it) {
                            const Layer *layer     = *layer_it;
                            size_t       layer_idx = layer_it - print_object.layers().begin();
                            if (input_expolygons[layer_idx].empty() || facet[0].z() > layer->slice_z || layer->slice_z > facet[2].z())
                                continue;

                            // https://kandepet.com/3d-printing-slicing-3d-objects/
                            float t            = (float(layer->slice_z) - facet[0].z()) / (facet[2].z() - facet[0].z());
                            Vec3f line_start_f = facet[0] + t * (facet[2] - facet[0]);
                            Vec3f line_end_f;

                            if (facet[1].z() > layer->slice_z) {
                                // [P0, P2] and [P0, P1]
                                float t1   = (float(layer->slice_z) - facet[0].z()) / (facet[1].z() - facet[0].z());
                                line_end_f = facet[0] + t1 * (facet[1] - facet[0]);
                            } else {
                                // [P0, P2] and [P1, P2]
                                float t2   = (float(layer->slice_z) - facet[1].z()) / (facet[2].z() - facet[1].z());
                                line_end_f = facet[1] + t2 * (facet[2] - facet[1]);
                            }

                            Point line_start(scale_(line_start_f.x()), scale_(line_start_f.y()));
                            Point line_end(scale_(line_end_f.x()), scale_(line_end_f.y()));
                            line_start -= print_object.center_offset();
                            line_end   -= print_object.center_offset();

                            size_t mutex_idx = layer_idx & 0x3F;
                            assert(mutex_idx < painted_lines_mutex.size());

                            PaintedLineVisitor visitor(edge_grids[layer_idx], painted_lines[layer_idx], painted_lines_mutex[mutex_idx], 16);
                            visitor.line_to_test.a = line_start;
                            visitor.line_to_test.b = line_end;
                            visitor.color          = int(extruder_idx);
                            edge_grids[layer_idx].visit_cells_intersecting_line(line_start, line_end, visitor);
                        }
                    }
                }); // end of parallel_for
            }
        }); // end of parallel_for
    }
    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - projection of painted triangles - end";
    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - painted layers count: "
                             << std::count_if(painted_lines.begin(), painted_lines.end(), [](const std::vector<PaintedLine> &pl) { return !pl.empty(); });

    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - layers segmentation in parallel - begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, print_object.layers().size()), [&edge_grids, &input_expolygons, &painted_lines, &segmented_regions, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            auto comp = [&edge_grids, layer_idx](const PaintedLine &first, const PaintedLine &second) {
                Point first_start_p = edge_grids[layer_idx].contours()[first.contour_idx].segment_start(first.line_idx);
                return first.contour_idx < second.contour_idx ||
                       (first.contour_idx == second.contour_idx &&
                        (first.line_idx < second.line_idx ||
                         (first.line_idx == second.line_idx &&
                          ((first.projected_line.a - first_start_p).cast<double>().squaredNorm() < (second.projected_line.a - first_start_p).cast<double>().squaredNorm() ||
                           ((first.projected_line.a - first_start_p).cast<double>().squaredNorm() == (second.projected_line.a - first_start_p).cast<double>().squaredNorm() &&
                            (first.projected_line.b - first.projected_line.a).cast<double>().squaredNorm() < (second.projected_line.b - second.projected_line.a).cast<double>().squaredNorm())))));
            };

            std::sort(painted_lines[layer_idx].begin(), painted_lines[layer_idx].end(), comp);
            std::vector<PaintedLine> &painted_lines_single = painted_lines[layer_idx];

            if (!painted_lines_single.empty()) {
                std::vector<std::vector<ColoredLine>> color_poly = colorize_polygons(edge_grids[layer_idx].contours(), painted_lines_single);
                assert(!color_poly.empty());
                assert(!color_poly.front().empty());
                if (has_layer_only_one_color(color_poly)) {
                    // If the whole layer is painted using the same color, it is not needed to construct a Voronoi diagram for the segmentation of this layer.
                    for (const ExPolygon &ex_polygon : input_expolygons[layer_idx])
                        segmented_regions[layer_idx].emplace_back(ex_polygon, size_t(color_poly.front().front().color));
                } else {
                    MMU_Graph graph = build_graph(layer_idx, color_poly);
                    remove_multiple_edges_in_vertices(graph, color_poly);
                    graph.remove_nodes_with_one_arc();

#ifdef MMU_SEGMENTATION_DEBUG_GRAPH
                    {
                        static int iRun = 0;
                        export_graph_to_svg(debug_out_path("mm-graph-final-%d-%d.svg", layer_idx, iRun++), graph, input_expolygons[layer_idx]);
                    }
#endif // MMU_SEGMENTATION_DEBUG_GRAPH

                    std::vector<std::pair<Polygon, size_t>> segmentation = extract_colored_segments(graph);
                    for (std::pair<Polygon, size_t> &region : segmentation)
                        segmented_regions[layer_idx].emplace_back(std::move(region));
                }

#ifdef MMU_SEGMENTATION_DEBUG_REGIONS
                {
                    static int iRun = 0;
                    export_regions_to_svg(debug_out_path("mm-regions-sides-%d-%d.svg", layer_idx, iRun++), segmented_regions[layer_idx], input_expolygons[layer_idx]);
                }
#endif // MMU_SEGMENTATION_DEBUG_REGIONS
            }
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - layers segmentation in parallel - end";
    throw_on_cancel_callback();

    if (auto w = print_object.config().mmu_segmented_region_max_width; w > 0.f) {
        cut_segmented_layers(input_expolygons, segmented_regions, float(-scale_(w)), throw_on_cancel_callback);
        throw_on_cancel_callback();
    }

//    return segmented_regions;
    std::vector<std::vector<ExPolygons>>                   top_and_bottom_layers    = mmu_segmentation_top_and_bottom_layers(print_object, input_expolygons, throw_on_cancel_callback);
    throw_on_cancel_callback();

    std::vector<std::vector<std::pair<ExPolygon, size_t>>> segmented_regions_merged = merge_segmented_layers(segmented_regions, std::move(top_and_bottom_layers), throw_on_cancel_callback);
    throw_on_cancel_callback();

#ifdef MMU_SEGMENTATION_DEBUG_REGIONS
    {
        static int iRun = 0;
        for (size_t layer_idx = 0; layer_idx < print_object.layers().size(); ++layer_idx)
            export_regions_to_svg(debug_out_path("mm-regions-merged-%d-%d.svg", layer_idx, iRun++), segmented_regions_merged[layer_idx], input_expolygons[layer_idx]);
    }
#endif // MMU_SEGMENTATION_DEBUG_REGIONS

    return segmented_regions_merged;
}

} // namespace Slic3r
