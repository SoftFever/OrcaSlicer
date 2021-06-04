#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "EdgeGrid.hpp"
#include "Layer.hpp"
#include "Print.hpp"
#include "VoronoiVisualUtils.hpp"

#include <utility>
#include <cfloat>
#include <unordered_set>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/segment.hpp>
#include <boost/geometry/index/rtree.hpp>

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
    PaintedLineVisitor(const EdgeGrid::Grid &grid, std::vector<PaintedLine> &painted_lines, size_t reserve) : grid(grid), painted_lines(painted_lines)
    {
        painted_lines_set.reserve(reserve);
    }

    void reset() { painted_lines_set.clear(); }

    bool operator()(coord_t iy, coord_t ix)
    {
        // Called with a row and column of the grid cell, which is intersected by a line.
        auto         cell_data_range = grid.cell_data_range(iy, ix);
        const Vec2d  v1              = line_to_test.vector().cast<double>();
        for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {
            Line        grid_line = grid.line(*it_contour_and_segment);
            const Vec2d v2        = grid_line.vector().cast<double>();
            // When lines have too different length, it is necessary to normalize them
            if (Slic3r::sqr(v1.dot(v2)) > cos_threshold2 * v1.squaredNorm() * v2.squaredNorm()) {
                // The two vectors are nearly collinear (their mutual angle is lower than 30 degrees)
                if (painted_lines_set.find(*it_contour_and_segment) == painted_lines_set.end()) {
                    double dist_1     = grid_line.distance_to(line_to_test.a);
                    double dist_2     = grid_line.distance_to(line_to_test.b);
                    double dist_3     = line_to_test.distance_to(grid_line.a);
                    double dist_4     = line_to_test.distance_to(grid_line.b);
                    double total_dist = std::min(std::min(dist_1, dist_2), std::min(dist_3, dist_4));

                    if (total_dist < 50 * SCALED_EPSILON) {
                        Line line_to_test_projected;
                        project_line_on_line(grid_line, line_to_test, &line_to_test_projected);

                        if (Line(grid_line.a, line_to_test_projected.a).length() > Line(grid_line.a, line_to_test_projected.b).length()) {
                            line_to_test_projected.reverse();
                        }
                        painted_lines.push_back({it_contour_and_segment->first, it_contour_and_segment->second, line_to_test_projected, this->color});
                        painted_lines_set.insert(*it_contour_and_segment);
                    }
                }
            }
        }
        // Continue traversing the grid along the edge.
        return true;
    }

    const EdgeGrid::Grid                                                                 &grid;
    std::vector<PaintedLine>                                                             &painted_lines;
    Line                                                                                  line_to_test;
    std::unordered_set<std::pair<size_t, size_t>, boost::hash<std::pair<size_t, size_t>>> painted_lines_set;
    int                                                                                   color = -1;

    static inline const double                                                            cos_threshold2 = Slic3r::sqr(cos(M_PI * 30. / 180.));
};

static std::vector<ColoredLine> to_colored_lines(const Polygon &polygon, int color)
{
    std::vector<ColoredLine> lines;
    if (polygon.points.size() > 2) {
        lines.reserve(polygon.points.size());
        for (auto it = polygon.points.begin(); it != polygon.points.end() - 1; ++it)
            lines.push_back({Line(*it, *(it + 1)), color});
        lines.push_back({Line(polygon.points.back(), polygon.points.front()), color});
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

// Double vertex equal to a coord_t point after conversion to double.
static bool vertex_equal_to_point(const Voronoi::VD::vertex_type &vertex, const Point &ipt)
{
    // Convert ipt to doubles, force the 80bit FPU temporary to 64bit and then compare.
    // This should work with any settings of math compiler switches and the C++ compiler
    // shall understand the memcpies as type punning and it shall optimize them out.
    using ulp_cmp_type = boost::polygon::detail::ulp_comparison<double>;
    ulp_cmp_type ulp_cmp;
    static constexpr int ULPS = boost::polygon::voronoi_diagram_traits<double>::vertex_equality_predicate_type::ULPS;
    return ulp_cmp(vertex.x(), double(ipt.x()), ULPS) == ulp_cmp_type::EQUAL &&
           ulp_cmp(vertex.y(), double(ipt.y()), ULPS) == ulp_cmp_type::EQUAL;
}

static inline bool vertex_equal_to_point(const Voronoi::VD::vertex_type *vertex, const Point &ipt) {
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
    for (size_t line_idx = start_idx; line_idx <= end_idx; ++line_idx) { internal_painted.emplace_back(painted_lines[line_idx]); }
    const int                filter_eps_value = scale_(0.1f);
    std::vector<PaintedLine> filtered_lines;
    filtered_lines.emplace_back(internal_painted.front());
    for (size_t line_idx = 1; line_idx < internal_painted.size(); ++line_idx) {
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

    std::vector<ColoredLine> colored_lines_simpl;
    colored_lines_simpl.emplace_back(final_lines.front());
    for (size_t line_idx = 1; line_idx < final_lines.size(); ++line_idx) {
        const ColoredLine &line_0 = final_lines[line_idx];

        if (colored_lines_simpl.back().color == line_0.color)
            colored_lines_simpl.back().line.b = line_0.line.b;
        else
            colored_lines_simpl.emplace_back(line_0);
    }

    final_lines = colored_lines_simpl;

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

static std::vector<ColoredLine> colorize_polygon(const Polygon &poly, const size_t start_idx, const size_t end_idx, std::vector<PaintedLine> &painted_lines)
{
    std::vector<ColoredLine> new_lines;
    Lines                    lines = poly.lines();

    for (size_t idx = 0; idx < painted_lines[start_idx].line_idx; ++idx)
        new_lines.emplace_back(ColoredLine{lines[idx], 0});

    for (size_t first_idx = start_idx; first_idx <= end_idx; ++first_idx) {
        size_t second_idx = first_idx;
        while (second_idx <= end_idx && painted_lines[first_idx].line_idx == painted_lines[second_idx].line_idx) ++second_idx;
        --second_idx;

        assert(painted_lines[first_idx].line_idx == painted_lines[second_idx].line_idx);
        std::vector<ColoredLine> lines_c_line = colorize_line(lines[painted_lines[first_idx].line_idx], first_idx, second_idx, painted_lines);
        new_lines.insert(new_lines.end(), lines_c_line.begin(), lines_c_line.end());

        if (second_idx + 1 <= end_idx)
            for (size_t idx = painted_lines[second_idx].line_idx + 1; idx < painted_lines[second_idx + 1].line_idx; ++idx)
                new_lines.emplace_back(ColoredLine{lines[idx], 0});

        first_idx = second_idx;
    }

    for (size_t idx = painted_lines[end_idx].line_idx + 1; idx < poly.size(); ++idx)
        new_lines.emplace_back(ColoredLine{lines[idx], 0});

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

static std::vector<std::vector<ColoredLine>> colorize_polygons(const Polygons &polygons, std::vector<PaintedLine> &painted_lines)
{
    const size_t start_idx = 0;
    const size_t end_idx   = painted_lines.size() - 1;

    std::vector<std::vector<ColoredLine>> new_polygons;

    for (size_t idx = 0; idx < painted_lines[start_idx].contour_idx; ++idx)
        new_polygons.emplace_back(to_colored_lines(polygons[idx], 0));

    for (size_t first_idx = start_idx; first_idx <= end_idx; ++first_idx) {
        size_t second_idx = first_idx;
        while (second_idx <= end_idx && painted_lines[first_idx].contour_idx == painted_lines[second_idx].contour_idx)
            ++second_idx;
        --second_idx;

        assert(painted_lines[first_idx].contour_idx == painted_lines[second_idx].contour_idx);
        std::vector<ColoredLine> polygon_c = colorize_polygon(polygons[painted_lines[first_idx].contour_idx], first_idx, second_idx, painted_lines);
        new_polygons.emplace_back(polygon_c);

        if (second_idx + 1 <= end_idx)
            for (size_t idx = painted_lines[second_idx].contour_idx + 1; idx < painted_lines[second_idx + 1].contour_idx; ++idx)
                new_polygons.emplace_back(to_colored_lines(polygons[idx], 0));

        first_idx = second_idx;
    }

    for (size_t idx = painted_lines[end_idx].contour_idx + 1; idx < polygons.size(); ++idx)
        new_polygons.emplace_back(to_colored_lines(polygons[idx], 0));

    return new_polygons;
}

using boost::polygon::voronoi_diagram;

struct MMU_Graph
{
    enum class ARC_TYPE { BORDER, NON_BORDER };

    struct Arc
    {
        size_t   from_idx;
        size_t   to_idx;
        int      color;
        ARC_TYPE type;
        bool     used{false};

        bool operator==(const Arc &rhs) const { return (from_idx == rhs.from_idx) && (to_idx == rhs.to_idx) && (color == rhs.color) && (type == rhs.type); }
        bool operator!=(const Arc &rhs) const { return !operator==(rhs); }
    };

    struct Node
    {
        Point                     point;
        std::list<MMU_Graph::Arc> neighbours;

        void remove_edge(const size_t to_idx)
        {
            for (auto arc_it = this->neighbours.begin(); arc_it != this->neighbours.end(); ++arc_it) {
                if (arc_it->to_idx == to_idx) {
                    assert(arc_it->type != ARC_TYPE::BORDER);
                    this->neighbours.erase(arc_it);
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
        nodes[from_idx].remove_edge(to_idx);
        nodes[to_idx].remove_edge(from_idx);
    }

    size_t get_global_index(const size_t poly_idx, const size_t point_idx) const { return polygon_idx_offset[poly_idx] + point_idx; }

    void append_edge(const size_t &from_idx, const size_t &to_idx, int color = -1, ARC_TYPE type = ARC_TYPE::NON_BORDER)
    {
        // Don't append duplicate edges between the same nodes.
        for (const MMU_Graph::Arc &arc : this->nodes[from_idx].neighbours)
            if (arc.to_idx == to_idx)
                return;
        for (const MMU_Graph::Arc &arc : this->nodes[to_idx].neighbours)
            if (arc.to_idx == to_idx)
                return;

        this->nodes[from_idx].neighbours.push_back({from_idx, to_idx, color, type});
        this->nodes[to_idx].neighbours.push_back({to_idx, from_idx, color, type});
        this->arcs.push_back({from_idx, to_idx, color, type});
        this->arcs.push_back({to_idx, from_idx, color, type});
    }

    // Ignoring arcs in the opposite direction
    MMU_Graph::Arc get_arc(size_t idx) { return this->arcs[idx * 2]; }

    size_t nodes_count() const { return this->nodes.size(); }

    void remove_nodes_with_one_arc()
    {
        std::queue<size_t> update_queue;
        for (const MMU_Graph::Node &node : this->nodes)
            if (node.neighbours.size() == 1) update_queue.emplace(&node - &this->nodes.front());

        while (!update_queue.empty()) {
            size_t           node_from_idx = update_queue.front();
            MMU_Graph::Node &node_from     = this->nodes[update_queue.front()];
            update_queue.pop();
            if (node_from.neighbours.empty())
                continue;

            assert(node_from.neighbours.size() == 1);
            size_t           node_to_idx = node_from.neighbours.front().to_idx;
            MMU_Graph::Node &node_to     = this->nodes[node_to_idx];
            this->remove_edge(node_from_idx, node_to_idx);
            if (node_to.neighbours.size() == 1)
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

    inline bool is_edge_attach_to_contour(const voronoi_diagram<double>::const_edge_iterator &edge_iterator) const
    {
        return this->is_vertex_on_contour(edge_iterator->vertex0()) || this->is_vertex_on_contour(edge_iterator->vertex1());
    }

    inline bool is_edge_connecting_two_contour_vertices(const voronoi_diagram<double>::const_edge_iterator &edge_iterator) const
    {
        return this->is_vertex_on_contour(edge_iterator->vertex0()) && this->is_vertex_on_contour(edge_iterator->vertex1());
    }
};

namespace bg  = boost::geometry;
namespace bgm = boost::geometry::model;
namespace bgi = boost::geometry::index;

// float is needed because for coord_t bgi::intersects throws "bad numeric conversion: positive overflow"
using rtree_point_t = bgm::point<float, 2, boost::geometry::cs::cartesian>;
using rtree_t       = bgi::rtree<std::pair<rtree_point_t, size_t>, bgi::rstar<16, 4>>;

static inline rtree_point_t mk_rtree_point(const Point &pt) { return rtree_point_t(float(pt.x()), float(pt.y())); }

static inline Point mk_point(const Voronoi::VD::vertex_type *point) { return Point(coord_t(point->x()), coord_t(point->y())); }

static inline Point mk_point(const Voronoi::Internal::point_type &point) { return Point(coord_t(point.x()), coord_t(point.y())); }

static inline Point mk_point(const voronoi_diagram<double>::vertex_type &point) { return Point(coord_t(point.x()), coord_t(point.y())); }

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

static MMU_Graph build_graph(size_t layer_idx, const std::vector<std::vector<ColoredLine>> &color_poly)
{
    Geometry::VoronoiDiagram vd;
    std::vector<ColoredLine> lines_colored  = to_lines(color_poly);
    Polygons                 color_poly_tmp = colored_points_to_polygon(color_poly);
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
    for (const Point &point : points)
        graph.nodes.push_back({point});

    graph.add_contours(color_poly);
    init_polygon_indices(graph, color_poly, lines_colored);

    assert(graph.nodes.size() == lines_colored.size());

    // All Voronoi vertices are post-processes to merge very close vertices to single. Witch Eliminates issues with intersection edges.
    // Also, Voronoi vertices outside of the bounding of input polygons are throw away by marking them.
    auto append_voronoi_vertices_to_graph = [&graph, &color_poly_tmp, &vd]() -> void {
        auto is_equal_points = [](const Point &p1, const Point &p2) { return p1 == p2 || (p1 - p2).cast<double>().norm() <= 3 * SCALED_EPSILON; };

        BoundingBox bbox = get_extents(color_poly_tmp);
        bbox.offset(SCALED_EPSILON);
        // EdgeGrid is used for vertices near to contour and rtree for other vertices
        // FIXME Lukas H.: Get rid of EdgeGrid and rtree. Use only one structure for both cases.
        EdgeGrid::Grid grid;
        grid.set_bbox(bbox);
        grid.create(color_poly_tmp, coord_t(scale_(10.)));
        rtree_t rtree;
        for (const voronoi_diagram<double>::vertex_type &vertex : vd.vertices()) {
            vertex.color(-1);
            Point vertex_point = mk_point(vertex);

            const Point &first_point  = graph.nodes[graph.get_arc(vertex.incident_edge()->cell()->source_index()).from_idx].point;
            const Point &second_point = graph.nodes[graph.get_arc(vertex.incident_edge()->twin()->cell()->source_index()).from_idx].point;

            if (vertex_equal_to_point(&vertex, first_point)) {
                assert(vertex.color() != vertex.incident_edge()->cell()->source_index());
                assert(vertex.color() != vertex.incident_edge()->twin()->cell()->source_index());
                vertex.color(graph.get_arc(vertex.incident_edge()->cell()->source_index()).from_idx);
            } else if (vertex_equal_to_point(&vertex, second_point)) {
                assert(vertex.color() != vertex.incident_edge()->cell()->source_index());
                assert(vertex.color() != vertex.incident_edge()->twin()->cell()->source_index());
                vertex.color(graph.get_arc(vertex.incident_edge()->twin()->cell()->source_index()).from_idx);
            } else if (bbox.contains(vertex_point)) {
                EdgeGrid::Grid::ClosestPointResult cp = grid.closest_point_signed_distance(vertex_point, coord_t(3 * SCALED_EPSILON));
                if (cp.valid()) {
                    size_t global_idx      = graph.get_global_index(cp.contour_idx, cp.start_point_idx);
                    size_t global_idx_next = graph.get_global_index(cp.contour_idx, (cp.start_point_idx + 1) % color_poly_tmp[cp.contour_idx].points.size());
                    vertex.color(is_equal_points(vertex_point, graph.nodes[global_idx].point) ? global_idx : global_idx_next);
                } else {
                    if (rtree.empty()) {
                        rtree.insert(std::make_pair(mk_rtree_point(vertex_point), graph.nodes_count()));
                        vertex.color(graph.nodes_count());
                        graph.nodes.push_back({vertex_point});
                    } else {
                        std::vector<std::pair<rtree_point_t, size_t>> closest;
                        rtree.query(bgi::nearest(mk_rtree_point(vertex_point), 1), std::back_inserter(closest));
                        assert(!closest.empty());
                        rtree_point_t r_point = closest.front().first;
                        Point         closest_p(bg::get<0>(r_point), bg::get<1>(r_point));
                        if (Line(vertex_point, closest_p).length() > 3 * SCALED_EPSILON) {
                            rtree.insert(std::make_pair(mk_rtree_point(vertex_point), graph.nodes_count()));
                            vertex.color(graph.nodes_count());
                            graph.nodes.push_back({vertex_point});
                        } else {
                            vertex.color(closest.front().second);
                        }
                    }
                }
            }
        }
    };

    append_voronoi_vertices_to_graph();

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

    BoundingBox bbox = get_extents(color_poly_tmp);
    bbox.offset(scale_(10.));
    const double bbox_dim_max = double(std::max(bbox.size().x(), bbox.size().y()));

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
                const MMU_Graph::Arc &graph_arc = graph.get_arc(edge_it->cell()->source_index());
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
            const Point  v0       = mk_point(edge_it->vertex0());
            const Point  v1       = mk_point(edge_it->vertex1());
            const size_t from_idx = edge_it->vertex0()->color();
            const size_t to_idx   = edge_it->vertex1()->color();

            // Both points are on contour, so skip them. In cases of duplicate Voronoi vertices, skip edges between the same two points.
            if (graph.is_edge_connecting_two_contour_vertices(edge_it) || (edge_it->vertex0()->color() == edge_it->vertex1()->color())) continue;

            const Line        edge_line(v0, v1);
            const Line        contour_line      = lines_colored[edge_it->cell()->source_index()].line;
            const ColoredLine colored_line      = lines_colored[edge_it->cell()->source_index()];
            const ColoredLine contour_line_prev = get_prev_contour_line(edge_it);
            const ColoredLine contour_line_next = get_next_contour_line(edge_it);

            Point intersection;
            if (edge_it->vertex0()->color() >= graph.nodes_count() || edge_it->vertex1()->color() >= graph.nodes_count()) {
//                if(edge_it->vertex0()->color() < graph.nodes_count() && !graph.is_vertex_on_contour(edge_it->vertex0())) {
//
//                }
                if (edge_it->vertex1()->color() < graph.nodes_count() && !graph.is_vertex_on_contour(edge_it->vertex1())) {
                    Line contour_line_twin = lines_colored[edge_it->twin()->cell()->source_index()].line;
                    if (line_intersection_with_epsilon(contour_line_twin, edge_line, &intersection)) {
                        const MMU_Graph::Arc &graph_arc = graph.get_arc(edge_it->twin()->cell()->source_index());
                        const size_t          to_idx_l  = is_point_closer_to_beginning_of_line(contour_line_twin, intersection) ? graph_arc.from_idx :
                                                                                                                                  graph_arc.to_idx;
                        graph.append_edge(edge_it->vertex1()->color(), to_idx_l);
                    } else if (line_intersection_with_epsilon(contour_line, edge_line, &intersection)) {
                        const MMU_Graph::Arc &graph_arc = graph.get_arc(edge_it->cell()->source_index());
                        const size_t to_idx_l = is_point_closer_to_beginning_of_line(contour_line, intersection) ? graph_arc.from_idx : graph_arc.to_idx;
                        graph.append_edge(edge_it->vertex1()->color(), to_idx_l);
                    }
                    mark_processed(edge_it);
                }
            } else if (graph.is_edge_attach_to_contour(edge_it)) {
                mark_processed(edge_it);
                // Skip edges witch connection two points on a contour
                if (graph.is_edge_connecting_two_contour_vertices(edge_it))
                    continue;

                if (graph.is_vertex_on_contour(edge_it->vertex0())) {
                    if (is_point_closer_to_beginning_of_line(contour_line, v0)) {
                        if ((!has_same_color(contour_line_prev, colored_line) || force_edge_adding[colored_line.poly_idx]) && points_inside(contour_line_prev.line, contour_line, v1)) {
                            graph.append_edge(from_idx, to_idx);
                            force_edge_adding[colored_line.poly_idx] = false;
                        }
                    } else {
                        if ((!has_same_color(contour_line_next, colored_line) || force_edge_adding[colored_line.poly_idx]) && points_inside(contour_line, contour_line_next.line, v1)) {
                            graph.append_edge(from_idx, to_idx);
                            force_edge_adding[colored_line.poly_idx] = false;
                        }
                    }
                } else {
                    assert(graph.is_vertex_on_contour(edge_it->vertex1()));
                    if (is_point_closer_to_beginning_of_line(contour_line, v1)) {
                        if ((!has_same_color(contour_line_prev, colored_line) || force_edge_adding[colored_line.poly_idx]) && points_inside(contour_line_prev.line, contour_line, v0)) {
                            graph.append_edge(from_idx, to_idx);
                            force_edge_adding[colored_line.poly_idx] = false;
                        }
                    } else {
                        if ((!has_same_color(contour_line_next, colored_line) || force_edge_adding[colored_line.poly_idx]) && points_inside(contour_line, contour_line_next.line, v0)) {
                            graph.append_edge(from_idx, to_idx);
                            force_edge_adding[colored_line.poly_idx] = false;
                        }
                    }
                }
            } else if (line_intersection_with_epsilon(contour_line, edge_line, &intersection)) {
                mark_processed(edge_it);
                Point real_v0 = graph.nodes[edge_it->vertex0()->color()].point;
                Point real_v1 = graph.nodes[edge_it->vertex1()->color()].point;

                if (is_point_closer_to_beginning_of_line(contour_line, intersection)) {
                    Line first_part(intersection, real_v0);
                    Line second_part(intersection, real_v1);

                    if (!has_same_color(contour_line_prev, colored_line)) {
                        if (points_inside(contour_line_prev.line, contour_line, first_part.b)) {
                            graph.append_edge(edge_it->vertex0()->color(), graph.get_arc(edge_it->cell()->source_index()).from_idx);
                        }
                        if (points_inside(contour_line_prev.line, contour_line, second_part.b)) {
                            graph.append_edge(edge_it->vertex1()->color(), graph.get_arc(edge_it->cell()->source_index()).from_idx);
                        }
                    }
                } else {
                    const size_t int_point_idx = graph.get_arc(edge_it->cell()->source_index()).to_idx;
                    const Point  int_point     = graph.nodes[int_point_idx].point;

                    const Line first_part(int_point, real_v0);
                    const Line second_part(int_point, real_v1);

                    if (!has_same_color(contour_line_next, colored_line)) {
                        if (points_inside(contour_line, contour_line_next.line, first_part.b)) {
                            graph.append_edge(edge_it->vertex0()->color(), int_point_idx);
                        }
                        if (points_inside(contour_line, contour_line_next.line, second_part.b)) {
                            graph.append_edge(edge_it->vertex1()->color(), int_point_idx);
                        }
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

static inline Polygon to_polygon(const Lines &lines)
{
    Polygon poly_out;
    poly_out.points.reserve(lines.size());
    for (const Line &line : lines)
        poly_out.points.emplace_back(line.a);
    return poly_out;
}

// Returns list of polygons and assigned colors.
// It iterates through all nodes on the border between two different colors, and from this point,
// start selection always left most edges for every node to construct CCW polygons.
// Assumes that graph is planar (without self-intersection edges)
static std::vector<std::pair<Polygon, size_t>> extract_colored_segments(MMU_Graph &graph)
{
    // When there is no next arc, then is returned original_arc or edge with is marked as used
    auto get_next = [&graph](const Line &process_line, MMU_Graph::Arc &original_arc) -> MMU_Graph::Arc & {
        std::vector<std::pair<MMU_Graph::Arc *, double>> sorted_arcs;
        for (MMU_Graph::Arc &arc : graph.nodes[original_arc.to_idx].neighbours) {
            if (graph.nodes[arc.to_idx].point == process_line.a || arc.used)
                continue;

            assert(original_arc.to_idx == arc.from_idx);
            Vec2d process_line_vec_n   = (process_line.a - process_line.b).cast<double>().normalized();
            Vec2d neighbour_line_vec_n = (graph.nodes[arc.to_idx].point - graph.nodes[arc.from_idx].point).cast<double>().normalized();

            double angle = ::acos(std::clamp(neighbour_line_vec_n.dot(process_line_vec_n), -1.0, 1.0));
            if (Slic3r::cross2(neighbour_line_vec_n, process_line_vec_n) < 0.0)
                angle = 2.0 * (double) PI - angle;

            sorted_arcs.emplace_back(&arc, angle);
        }

        std::sort(sorted_arcs.begin(), sorted_arcs.end(),
                  [](std::pair<MMU_Graph::Arc *, double> &l, std::pair<MMU_Graph::Arc *, double> &r) -> bool { return l.second < r.second; });

        // Try to return left most edge witch is unused
        for (auto &sorted_arc : sorted_arcs)
            if (!sorted_arc.first->used)
                return *sorted_arc.first;

        if (sorted_arcs.empty())
            return original_arc;

        return *(sorted_arcs.front().first);
    };

    std::vector<std::pair<Polygon, size_t>> polygons_segments;
    for (MMU_Graph::Node &node : graph.nodes)
        for (MMU_Graph::Arc &arc : node.neighbours)
            arc.used = false;

    for (size_t node_idx = 0; node_idx < graph.all_border_points; ++node_idx) {
        MMU_Graph::Node &node = graph.nodes[node_idx];

        for (MMU_Graph::Arc &arc : node.neighbours) {
            if (arc.type == MMU_Graph::ARC_TYPE::NON_BORDER || arc.used) continue;

            Line process_line(node.point, graph.nodes[arc.to_idx].point);
            arc.used = true;

            Lines face_lines;
            face_lines.emplace_back(process_line);
            Point start_p = process_line.a;

            Line            p_vec = process_line;
            MMU_Graph::Arc *p_arc = &arc;
            do {
                MMU_Graph::Arc &next = get_next(p_vec, *p_arc);
                face_lines.emplace_back(Line(graph.nodes[next.from_idx].point, graph.nodes[next.to_idx].point));
                if (next.used) break;

                next.used = true;
                p_vec     = Line(graph.nodes[next.from_idx].point, graph.nodes[next.to_idx].point);
                p_arc     = &next;
            } while (graph.nodes[p_arc->to_idx].point != start_p);

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
static inline double compute_edge_length(MMU_Graph &graph, size_t start_idx, MMU_Graph::Arc &start_edge)
{
    for (MMU_Graph::Node &node : graph.nodes)
        for (MMU_Graph::Arc &arc : node.neighbours)
            arc.used = false;

    start_edge.used                   = true;
    MMU_Graph::Arc *arc               = &start_edge;
    size_t          idx               = start_idx;
    double          line_total_length = Line(graph.nodes[idx].point, graph.nodes[arc->to_idx].point).length();
    while (graph.nodes[arc->to_idx].neighbours.size() == 2) {
        bool found = false;
        for (MMU_Graph::Arc &arc_n : graph.nodes[arc->to_idx].neighbours) {
            if (arc_n.type == MMU_Graph::ARC_TYPE::NON_BORDER && !arc_n.used && arc_n.to_idx != idx) {
                Line first_line(graph.nodes[idx].point, graph.nodes[arc->to_idx].point);
                Line second_line(graph.nodes[arc->to_idx].point, graph.nodes[arc_n.to_idx].point);

                Vec2d  first_line_vec    = (first_line.a - first_line.b).cast<double>();
                Vec2d  second_line_vec   = (second_line.b - second_line.a).cast<double>();
                Vec2d  first_line_vec_n  = first_line_vec.normalized();
                Vec2d  second_line_vec_n = second_line_vec.normalized();
                double angle             = ::acos(std::clamp(first_line_vec_n.dot(second_line_vec_n), -1.0, 1.0));
                if (Slic3r::cross2(first_line_vec_n, second_line_vec_n) < 0.0)
                    angle = 2.0 * (double) PI - angle;

                if (std::abs(angle - PI) >= (PI / 12))
                    continue;

                idx = arc->to_idx;
                arc = &arc_n;

                line_total_length += Line(graph.nodes[idx].point, graph.nodes[arc->to_idx].point).length();
                arc_n.used = true;
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
            Line   seg_line(graph.nodes[first_idx].point, graph.nodes[second_idx].point);

            if (graph.nodes[first_idx].neighbours.size() >= 3) {
                std::vector<std::pair<MMU_Graph::Arc *, double>> arc_to_check;
                for (MMU_Graph::Arc &n_arc : graph.nodes[first_idx].neighbours) {
                    if (n_arc.type == MMU_Graph::ARC_TYPE::NON_BORDER) {
                        double total_len = compute_edge_length(graph, first_idx, n_arc);
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
    tbb::parallel_for(tbb::blocked_range<size_t>(0, segmented_regions.size()),[&](const tbb::blocked_range<size_t>& range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            std::vector<std::pair<ExPolygon, size_t>> segmented_regions_cuts;
            for (const std::pair<ExPolygon, size_t> &colored_expoly : segmented_regions[layer_idx]) {
                ExPolygons cut_colored_expoly = diff_ex(colored_expoly.first, offset_ex(input_expolygons[layer_idx], cut_width));
                for (const ExPolygon &expoly : cut_colored_expoly) {
                    segmented_regions_cuts.emplace_back(expoly, colored_expoly.second);
                }
            }
            segmented_regions[layer_idx] = segmented_regions_cuts;
        }
    }); // end of parallel_for
}

// Returns MMU segmentation of top and bottom layers based on painting in MMU segmentation gizmo
static inline std::vector<std::vector<ExPolygons>> mmu_segmentation_top_and_bottom_layers(const PrintObject             &print_object,
                                                                                          const std::vector<ExPolygons> &input_expolygons,
                                                                                          const std::function<void()>   &throw_on_cancel_callback)
{
    const size_t num_extruders = print_object.print()->config().nozzle_diameter.size();
    const ConstLayerPtrsAdaptor layers = print_object.layers();
    std::vector<std::vector<ExPolygons>> triangles_by_color(num_extruders);
    triangles_by_color.assign(num_extruders, std::vector<ExPolygons>(layers.size()));
    for (const ModelVolume *mv : print_object.model_object()->volumes) {
        for (size_t extruder_idx = 0; extruder_idx < num_extruders; ++extruder_idx) {
            throw_on_cancel_callback();
            const indexed_triangle_set custom_facets = mv->mmu_segmentation_facets.get_facets(*mv, EnforcerBlockerType(extruder_idx));
            if (!mv->is_model_part() || custom_facets.indices.empty())
                continue;

            const Transform3f tr = print_object.trafo().cast<float>() * mv->get_matrix().cast<float>();
            for (size_t facet_idx = 0; facet_idx < custom_facets.indices.size(); ++facet_idx) {
                float min_z = std::numeric_limits<float>::max();
                float max_z = std::numeric_limits<float>::lowest();

                std::array<Vec3f, 3> facet;
                Points               projected_facet(3);
                for (int p_idx = 0; p_idx < 3; ++p_idx) {
                    facet[p_idx] = tr * custom_facets.vertices[custom_facets.indices[facet_idx](p_idx)];
                    max_z        = std::max(max_z, facet[p_idx].z());
                    min_z        = std::min(min_z, facet[p_idx].z());
                }

                // Sort the vertices by z-axis for simplification of projected_facet on slices
                std::sort(facet.begin(), facet.end(), [](const Vec3f &p1, const Vec3f &p2) { return p1.z() < p2.z(); });

                for (int p_idx = 0; p_idx < 3; ++p_idx) {
                    projected_facet[p_idx] = Point(scale_(facet[p_idx].x()), scale_(facet[p_idx].y()));
                    projected_facet[p_idx] = projected_facet[p_idx] - print_object.center_offset();
                }

                ExPolygon triangle = ExPolygon(projected_facet);

                // Find lowest slice not below the triangle.
                auto first_layer = std::upper_bound(layers.begin(), layers.end(), float(min_z - EPSILON),
                                                    [](float z, const Layer *l1) { return z < l1->slice_z + l1->height * 0.5; });
                auto last_layer  = std::upper_bound(layers.begin(), layers.end(), float(max_z - EPSILON),
                                                   [](float z, const Layer *l1) { return z < l1->slice_z + l1->height * 0.5; });

                if (last_layer == layers.end())
                    --last_layer;

                if (first_layer == layers.end() || (first_layer != layers.begin() && facet[0].z() < (*first_layer)->print_z - EPSILON))
                    --first_layer;

                for (auto layer_it = first_layer; (layer_it != (last_layer + 1) && layer_it != layers.end()); ++layer_it) {
                    size_t layer_idx = layer_it - layers.begin();
                    triangles_by_color[extruder_idx][layer_idx].emplace_back(triangle);
                }
            }
        }
    }

    auto get_extrusion_width = [&layers = std::as_const(layers)](const size_t layer_idx) -> float {
        auto extrusion_width_it = std::max_element(layers[layer_idx]->regions().begin(), layers[layer_idx]->regions().end(),
                                                   [](const LayerRegion *l1, const LayerRegion *l2) {
                                                       return l1->region().config().perimeter_extrusion_width <
                                                              l2->region().config().perimeter_extrusion_width;
                                                   });
        assert(extrusion_width_it != layers[layer_idx]->regions().end());
        return float((*extrusion_width_it)->region().config().perimeter_extrusion_width);
    };

    auto get_top_solid_layers = [&layers = std::as_const(layers)](const size_t layer_idx) -> int {
        auto top_solid_layer_it = std::max_element(layers[layer_idx]->regions().begin(), layers[layer_idx]->regions().end(),
                                                   [](const LayerRegion *l1, const LayerRegion *l2) {
                                                       return l1->region().config().top_solid_layers < l2->region().config().top_solid_layers;
                                                   });
        assert(top_solid_layer_it != layers[layer_idx]->regions().end());
        return (*top_solid_layer_it)->region().config().top_solid_layers;
    };

    auto get_bottom_solid_layers = [&layers = std::as_const(layers)](const size_t layer_idx) -> int {
        auto top_bottom_layer_it = std::max_element(layers[layer_idx]->regions().begin(), layers[layer_idx]->regions().end(),
                                                    [](const LayerRegion *l1, const LayerRegion *l2) {
                                                        return l1->region().config().bottom_solid_layers < l2->region().config().bottom_solid_layers;
                                                    });
        assert(top_bottom_layer_it != layers[layer_idx]->regions().end());
        return (*top_bottom_layer_it)->region().config().bottom_solid_layers;
    };

    std::vector<ExPolygons> top_layers(input_expolygons.size());
    top_layers.back() = input_expolygons.back();
    tbb::parallel_for(tbb::blocked_range<size_t>(1, input_expolygons.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            float extrusion_width     = 0.1f * float(scale_(get_extrusion_width(layer_idx)));
            top_layers[layer_idx - 1] = diff_ex(input_expolygons[layer_idx - 1], offset_ex(input_expolygons[layer_idx], extrusion_width));
        }
    }); // end of parallel_for

    std::vector<ExPolygons> bottom_layers(input_expolygons.size());
    bottom_layers.front() = input_expolygons.front();
    tbb::parallel_for(tbb::blocked_range<size_t>(0, input_expolygons.size() - 1), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            float extrusion_width        = 0.1f * float(scale_(get_extrusion_width(layer_idx)));
            bottom_layers[layer_idx + 1] = diff_ex(input_expolygons[layer_idx + 1], offset_ex(input_expolygons[layer_idx], extrusion_width));
        }
    }); // end of parallel_for

    tbb::parallel_for(tbb::blocked_range<size_t>(0, input_expolygons.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            float extrusion_width = 0.1f * float(scale_(get_extrusion_width(layer_idx)));
            for (std::vector<ExPolygons> &triangles : triangles_by_color) {
                if (!triangles[layer_idx].empty() && (!top_layers[layer_idx].empty() || !bottom_layers[layer_idx].empty())) {
                    ExPolygons connected = union_ex(offset_ex(triangles[layer_idx], float(10 * SCALED_EPSILON)));
                    triangles[layer_idx] = union_ex(offset_ex(offset_ex(connected, -extrusion_width / 1), extrusion_width / 1));
                } else {
                    triangles[layer_idx].clear();
                }
            }
        }
    }); // end of parallel_for

    std::vector<std::vector<ExPolygons>> triangles_by_color_bottom(num_extruders);
    std::vector<std::vector<ExPolygons>> triangles_by_color_top(num_extruders);
    triangles_by_color_bottom.assign(num_extruders, std::vector<ExPolygons>(input_expolygons.size()));
    triangles_by_color_top.assign(num_extruders, std::vector<ExPolygons>(input_expolygons.size()));

    for (size_t layer_idx = 0; layer_idx < input_expolygons.size(); ++layer_idx) {
        BOOST_LOG_TRIVIAL(debug) << "MMU segmentation of top layer: " << layer_idx;
        float      extrusion_width  = scale_(get_extrusion_width(layer_idx));
        int        top_solid_layers = get_top_solid_layers(layer_idx);
        ExPolygons top_expolygon    = top_layers[layer_idx];
        if (top_expolygon.empty())
            continue;

        for (size_t color_idx = 0; color_idx < triangles_by_color.size(); ++color_idx) {
            throw_on_cancel_callback();
            if (triangles_by_color[color_idx][layer_idx].empty())
                continue;
            ExPolygons intersection_poly = intersection_ex(triangles_by_color[color_idx][layer_idx], top_expolygon);
            if (!intersection_poly.empty()) {
                triangles_by_color_top[color_idx][layer_idx].insert(triangles_by_color_top[color_idx][layer_idx].end(), intersection_poly.begin(),
                                                                    intersection_poly.end());
                for (int last_idx = int(layer_idx) - 1; last_idx >= std::max(int(layer_idx - top_solid_layers), int(0)); --last_idx) {
                    float offset_value = float(layer_idx - last_idx) * (-1.0f) * extrusion_width;
                    if (offset_ex(top_expolygon, offset_value).empty())
                        continue;
                    ExPolygons layer_slices_trimmed = input_expolygons[last_idx];

                    for (int last_idx_1 = last_idx; last_idx_1 < int(layer_idx); ++last_idx_1) {
                        layer_slices_trimmed = intersection_ex(layer_slices_trimmed, input_expolygons[last_idx_1 + 1]);
                    }

                    ExPolygons offset_e            = offset_ex(layer_slices_trimmed, offset_value);
                    ExPolygons intersection_poly_2 = intersection_ex(triangles_by_color_top[color_idx][layer_idx], offset_e);
                    triangles_by_color_top[color_idx][last_idx].insert(triangles_by_color_top[color_idx][last_idx].end(), intersection_poly_2.begin(),
                                                                       intersection_poly_2.end());
                }
            }
        }
    }

    for (size_t layer_idx = 0; layer_idx < input_expolygons.size(); ++layer_idx) {
        BOOST_LOG_TRIVIAL(debug) << "MMU segmentation of bottom layer: " << layer_idx;
        float             extrusion_width     = scale_(get_extrusion_width(layer_idx));
        int               bottom_solid_layers = get_bottom_solid_layers(layer_idx);
        const ExPolygons &bottom_expolygon    = bottom_layers[layer_idx];
        if (bottom_expolygon.empty())
            continue;

        for (size_t color_idx = 0; color_idx < triangles_by_color.size(); ++color_idx) {
            throw_on_cancel_callback();
            if (triangles_by_color[color_idx][layer_idx].empty())
                continue;

            ExPolygons intersection_poly = intersection_ex(triangles_by_color[color_idx][layer_idx], bottom_expolygon);
            if (!intersection_poly.empty()) {
                triangles_by_color_bottom[color_idx][layer_idx].insert(triangles_by_color_bottom[color_idx][layer_idx].end(), intersection_poly.begin(),
                                                                       intersection_poly.end());
                for (size_t last_idx = layer_idx + 1; last_idx < std::min(layer_idx + bottom_solid_layers, input_expolygons.size()); ++last_idx) {
                    float offset_value = float(last_idx - layer_idx) * (-1.0f) * extrusion_width;
                    if (offset_ex(bottom_expolygon, offset_value).empty())
                        continue;
                    ExPolygons layer_slices_trimmed = input_expolygons[last_idx];
                    for (int last_idx_1 = int(last_idx); last_idx_1 > int(layer_idx); --last_idx_1) {
                        layer_slices_trimmed = intersection_ex(layer_slices_trimmed, offset_ex(input_expolygons[last_idx_1 - 1], offset_value));
                    }

                    ExPolygons offset_e            = offset_ex(layer_slices_trimmed, offset_value);
                    ExPolygons intersection_poly_2 = intersection_ex(triangles_by_color_bottom[color_idx][layer_idx], offset_e);
                    append(triangles_by_color_bottom[color_idx][last_idx], std::move(intersection_poly_2));
                }
            }
        }
    }

    std::vector<std::vector<ExPolygons>> triangles_by_color_merged(num_extruders);
    triangles_by_color_merged.assign(num_extruders, std::vector<ExPolygons>(input_expolygons.size()));
    for (size_t layer_idx = 0; layer_idx < input_expolygons.size(); ++layer_idx) {
        throw_on_cancel_callback();
        for (size_t color_idx = 0; color_idx < triangles_by_color_merged.size(); ++color_idx) {
            auto &self = triangles_by_color_merged[color_idx][layer_idx];
            append(self, std::move(triangles_by_color_bottom[color_idx][layer_idx]));
            append(self, std::move(triangles_by_color_top[color_idx][layer_idx]));
            self = union_ex(self);
        }

        // Cut all colors for cases when two colors are overlapping
        for (size_t color_idx = 1; color_idx < triangles_by_color_merged.size(); ++color_idx) {
            triangles_by_color_merged[color_idx][layer_idx] = diff_ex(triangles_by_color_merged[color_idx][layer_idx],
                                                                      triangles_by_color_merged[color_idx - 1][layer_idx]);
        }
    }

    return triangles_by_color_merged;
}

static std::vector<std::vector<std::pair<ExPolygon, size_t>>> merge_segmented_layers(
    const std::vector<std::vector<std::pair<ExPolygon, size_t>>> &segmented_regions,
    std::vector<std::vector<ExPolygons>>                        &&top_and_bottom_layers,
    const std::function<void()>                                  &throw_on_cancel_callback)
{
    std::vector<std::vector<std::pair<ExPolygon, size_t>>> segmented_regions_merged(segmented_regions.size());

    tbb::parallel_for(tbb::blocked_range<size_t>(0, segmented_regions.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            BOOST_LOG_TRIVIAL(debug) << "MMU segmentation - merging region: " << layer_idx;
            for (const std::pair<ExPolygon, size_t> &colored_expoly : segmented_regions[layer_idx]) {
                throw_on_cancel_callback();
                ExPolygons cut_colored_expoly = {colored_expoly.first};
                for (const std::vector<ExPolygons> &top_and_bottom_layer : top_and_bottom_layers)
                    cut_colored_expoly = diff_ex(cut_colored_expoly, top_and_bottom_layer[layer_idx]);
                for (ExPolygon &ex_poly : cut_colored_expoly)
                    segmented_regions_merged[layer_idx].emplace_back(std::move(ex_poly), colored_expoly.second);
            }

            for (size_t color_idx = 0; color_idx < top_and_bottom_layers.size(); ++color_idx)
                for (ExPolygon &expoly : top_and_bottom_layers[color_idx][layer_idx])
                    segmented_regions_merged[layer_idx].emplace_back(std::move(expoly), color_idx);
        }
    }); // end of parallel_for

    return segmented_regions_merged;
}

std::vector<std::vector<std::pair<ExPolygon, size_t>>> multi_material_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback)
{
    std::vector<std::vector<std::pair<ExPolygon, size_t>>> segmented_regions(print_object.layers().size());
    std::vector<std::vector<PaintedLine>>                  painted_lines(print_object.layers().size());
    std::vector<EdgeGrid::Grid>                            edge_grids(print_object.layers().size());
    const ConstLayerPtrsAdaptor                            layers = print_object.layers();
    std::vector<ExPolygons>                                input_expolygons(layers.size());
    std::vector<Polygons>                                  input_polygons(layers.size());

    throw_on_cancel_callback();

    // Merge all regions and remove small holes
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layers.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            ExPolygons ex_polygons;
            for (LayerRegion *region : layers[layer_idx]->regions())
                for (const Surface &surface : region->slices.surfaces)
                    Slic3r::append(ex_polygons, offset_ex(surface.expolygon, float(SCALED_EPSILON)));
            // All expolygons are expanded by SCALED_EPSILON, merged, and then shrunk again by SCALED_EPSILON
            // to ensure that very close polygons will be merged.
            ex_polygons = union_ex(ex_polygons);
            // Remove all expolygons and holes with an area less than 0.01mm^2
            remove_small_and_small_holes(ex_polygons, Slic3r::sqr(scale_(0.1f)));
            // Occasionally, some input polygons contained self-intersections that caused problems with Voronoi diagrams
            // and consequently with the extraction of colored segments by function extract_colored_segments.
            // Calling simplify_polygons removes these self-intersections.
            // Also, occasionally input polygons contained several points very close together (distance between points is 1 or so).
            // Such close points sometimes caused that the Voronoi diagram has self-intersecting edges around these vertices.
            // This consequently leads to issues with the extraction of colored segments by function extract_colored_segments.
            // Calling expolygons_simplify fixed these issues.
            input_expolygons[layer_idx] = simplify_polygons_ex(to_polygons(expolygons_simplify(offset_ex(ex_polygons, float(-SCALED_EPSILON)), SCALED_EPSILON)));
            input_polygons[layer_idx] = to_polygons(input_expolygons[layer_idx]);
        }
    }); // end of parallel_for

    for (size_t layer_idx = 0; layer_idx < layers.size(); ++layer_idx) {
        throw_on_cancel_callback();
        BoundingBox  bbox(get_extents(input_expolygons[layer_idx]));
        bbox.offset(SCALED_EPSILON);
        edge_grids[layer_idx].set_bbox(bbox);
        edge_grids[layer_idx].create(input_expolygons[layer_idx], coord_t(scale_(10.)));
    }

    for (const ModelVolume *mv : print_object.model_object()->volumes) {
        const size_t num_extruders = print_object.print()->config().nozzle_diameter.size();
        for (size_t extruder_idx = 1; extruder_idx < num_extruders; ++extruder_idx) {
            throw_on_cancel_callback();
            const indexed_triangle_set custom_facets = mv->mmu_segmentation_facets.get_facets(*mv, EnforcerBlockerType(extruder_idx));
            if (!mv->is_model_part() || custom_facets.indices.empty())
                continue;

            const Transform3f tr = print_object.trafo().cast<float>() * mv->get_matrix().cast<float>();
            for (size_t facet_idx = 0; facet_idx < custom_facets.indices.size(); ++facet_idx) {
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
                    if (facet[0].z() > layer->slice_z || layer->slice_z > facet[2].z())
                        continue;

                    // https://kandepet.com/3d-printing-slicing-3d-objects/
                    float t            = (float(layer->slice_z) - facet[0].z()) / (facet[2].z() - facet[0].z());
                    Vec3f line_start_f = facet[0] + t * (facet[2] - facet[0]);
                    Vec3f line_end_f;

                    if (facet[1].z() > layer->slice_z) {
                        // [P0, P2] a [P0, P1]
                        float t1   = (float(layer->slice_z) - facet[0].z()) / (facet[1].z() - facet[0].z());
                        line_end_f = facet[0] + t1 * (facet[1] - facet[0]);
                    } else if (facet[1].z() <= layer->slice_z) {
                        // [P0, P2] a [P1, P2]
                        float t2   = (float(layer->slice_z) - facet[1].z()) / (facet[2].z() - facet[1].z());
                        line_end_f = facet[1] + t2 * (facet[2] - facet[1]);
                    }

                    Point line_start(scale_(line_start_f.x()), scale_(line_start_f.y()));
                    Point line_end(scale_(line_end_f.x()), scale_(line_end_f.y()));
                    line_start -= print_object.center_offset();
                    line_end   -= print_object.center_offset();

                    PaintedLineVisitor       visitor(edge_grids[layer_idx], painted_lines[layer_idx], 16);
                    visitor.reset();
                    visitor.line_to_test.a = line_start;
                    visitor.line_to_test.b = line_end;
                    visitor.color          = int(extruder_idx);
                    edge_grids[layer_idx].visit_cells_intersecting_line(line_start, line_end, visitor);
                }
            }
        }
    }

    tbb::parallel_for(tbb::blocked_range<size_t>(0, print_object.layers().size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            //    for(size_t layer_idx = 0; layer_idx < print_object.layers().size(); ++layer_idx) {
            BOOST_LOG_TRIVIAL(debug) << "MMU segmentation of layer: " << layer_idx;
            auto comp = [&edge_grids, layer_idx](const PaintedLine &first, const PaintedLine &second) {
                Point first_start_p = *(edge_grids[layer_idx].contours()[first.contour_idx].begin() + first.line_idx);

                return first.contour_idx < second.contour_idx ||
                       (first.contour_idx == second.contour_idx &&
                        (first.line_idx < second.line_idx ||
                         (first.line_idx == second.line_idx &&
                          Line(first_start_p, first.projected_line.a).length() < Line(first_start_p, second.projected_line.a).length())));
            };

            std::sort(painted_lines[layer_idx].begin(), painted_lines[layer_idx].end(), comp);
            std::vector<PaintedLine> &painted_lines_single = painted_lines[layer_idx];

            if (!painted_lines_single.empty()) {
                std::vector<std::vector<ColoredLine>> color_poly = colorize_polygons(input_polygons[layer_idx], painted_lines_single);
                MMU_Graph                             graph      = build_graph(layer_idx, color_poly);
                remove_multiple_edges_in_vertices(graph, color_poly);
                graph.remove_nodes_with_one_arc();
                std::vector<std::pair<Polygon, size_t>> segmentation = extract_colored_segments(graph);
                for (const std::pair<Polygon, size_t> &region : segmentation)
                    segmented_regions[layer_idx].emplace_back(region);
            }
        }
    }); // end of parallel_for
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

    return segmented_regions_merged;
}

} // namespace Slic3r
