#include "LineSplit.hpp"

#include "AABBTreeLines.hpp"
#include "SVG.hpp"
#include "Utils.hpp"

//#define DEBUG_SPLIT_LINE

namespace Slic3r {
namespace Algorithm {

#ifdef DEBUG_SPLIT_LINE
static std::atomic<std::uint32_t> g_dbg_id = 0;
#endif

// Z for points from clip polygon
static constexpr auto CLIP_IDX = std::numeric_limits<ClipperLib_Z::cInt>::max();

static void cb_split_line(const ClipperZUtils::ZPoint& e1bot,
                   const ClipperZUtils::ZPoint& e1top,
                   const ClipperZUtils::ZPoint& e2bot,
                   const ClipperZUtils::ZPoint& e2top,
                   ClipperZUtils::ZPoint&       pt)
{
    coord_t zs[4]{e1bot.z(), e1top.z(), e2bot.z(), e2top.z()};
    std::sort(zs, zs + 4);
    pt.z() = -(zs[0] + 1);
}

static bool is_src(const ClipperZUtils::ZPoint& p) { return p.z() >= 0 && p.z() != CLIP_IDX; }
static bool is_clip(const ClipperZUtils::ZPoint& p) { return p.z() == CLIP_IDX; }
static bool is_new(const ClipperZUtils::ZPoint& p) { return p.z() < 0; }

static Point to_point(const ClipperZUtils::ZPoint& p) { return {p.x(), p.y()}; }

using SplitNode = std::vector<ClipperZUtils::ZPath*>;

// Note: p cannot be one of the line end
static bool point_on_line(const Point& p, const Line& l)
{
    // Check collinear
    const auto d1 = l.b - l.a;
    const auto d2 = p - l.a;
    if (d1.x() * d2.y() != d1.y() * d2.x()) { 
        return false;
    }

    // Make sure p is in between line.a and line.b
    if (l.a.x() != l.b.x())
        return (p.x() > l.a.x()) == (p.x() < l.b.x());
    else
        return (p.y() > l.a.y()) == (p.y() < l.b.y());
}
 
SplittedLine do_split_line(const ClipperZUtils::ZPath& path, const ExPolygons& clip, bool closed)
{
    assert(path.size() > 1);
#ifdef DEBUG_SPLIT_LINE
    const auto  dbg_path_points = ClipperZUtils::from_zpath<false>(path);
    BoundingBox dbg_bbox = get_extents(clip);
    dbg_bbox.merge(get_extents(dbg_path_points));
    dbg_bbox.offset(scale_(1.));
    const std::uint32_t dbg_id = g_dbg_id++;
    {
        ::Slic3r::SVG svg(debug_out_path("do_split_line_%d_input.svg", dbg_id).c_str(), dbg_bbox);
        svg.draw(clip, "red", 0.5);
        svg.draw_outline(clip, "red");
        svg.draw(Polyline{dbg_path_points});
        svg.draw(dbg_path_points);
        svg.Close();
    }
#endif

    ClipperZUtils::ZPaths intersections;
    // Perform an intersection
    {
        // Convert clip polygon to closed contours
        ClipperZUtils::ZPaths clip_path;
        for (const auto& exp : clip) {
            clip_path.emplace_back(ClipperZUtils::to_zpath<false>(exp.contour.points, CLIP_IDX));
            for (const Polygon& hole : exp.holes)
                clip_path.emplace_back(ClipperZUtils::to_zpath<false>(hole.points, CLIP_IDX));
        }

        ClipperLib_Z::Clipper zclipper;
        zclipper.PreserveCollinear(true);
        zclipper.ZFillFunction(cb_split_line);
        zclipper.AddPaths(clip_path, ClipperLib_Z::ptClip, true);
        zclipper.AddPath(path, ClipperLib_Z::ptSubject, false);
        ClipperLib_Z::PolyTree polytree;
        zclipper.Execute(ClipperLib_Z::ctIntersection, polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
        ClipperLib_Z::PolyTreeToPaths(std::move(polytree), intersections);
    }
    if (intersections.empty()) {
        return {};
    }

#ifdef DEBUG_SPLIT_LINE
    {
        int i = 0;
        for (const auto& segment : intersections) {
            ::Slic3r::SVG svg(debug_out_path("do_split_line_%d_seg_%d.svg", dbg_id, i).c_str(), dbg_bbox);
            svg.draw(clip, "red", 0.5);
            svg.draw_outline(clip, "red");
            const auto segment_points = ClipperZUtils::from_zpath<false>(segment);
            svg.draw(Polyline{segment_points});
            for (const ClipperZUtils::ZPoint& p : segment) {
                const auto z = p.z();
                if (is_new(p)) {
                    svg.draw(to_point(p), "yellow");
                } else if (is_clip(p)) {
                    svg.draw(to_point(p), "red");
                } else {
                    svg.draw(to_point(p), "black");
                }
            }
            svg.Close();
            i++;
        }
    }
#endif

    // Connect the intersection back to the remaining loop
    std::vector<SplitNode> split_chain;
    {
        // AABBTree over source paths.
        // Only built if necessary, that is if any of the clipped segment has first point came from clip polygon,
        // and we need to find out which source edge that point came from.
        AABBTreeLines::LinesDistancer<Line> aabb_tree;

        split_chain.assign(path.size(), {});
        for (ClipperZUtils::ZPath& segment : intersections) {
            assert(segment.size() >= 2);
            ClipperZUtils::ZPoint& front = segment.front();

            // FIXME: there might be cases that the point is also an existing point from the src path (i.e., src and clip shears a same point)
            if (is_clip(front)) {
                // The segment starts with a point from clip polygon, we must find out which line of the src path this point belongs to.
                const ClipperZUtils::ZPoint& next_point = segment[1];
                if (is_new(next_point)) {
                    // Same line
                    front.z() = next_point.z();
                } else if (is_src(next_point)) {
                    // Previous line
                    front.z() = -next_point.z();
                } else {
                    // Oops! We have no idea where this point came from, go through EVERY edge of src path and see which one this point lies on.
                    if (aabb_tree.get_lines().empty()) {
                        Lines lines;
                        lines.reserve(path.size() - 1);
                        for (auto it = path.begin() + 1; it != path.end(); ++it) {
                            lines.emplace_back(to_point(it[-1]), to_point(*it));
                        }
                        aabb_tree = AABBTreeLines::LinesDistancer(lines);
                    }
                    const Point p = to_point(front);
                    const auto possible_edges = aabb_tree.all_lines_in_radius(p, SCALED_EPSILON);
                    assert(!possible_edges.empty());
                    for (size_t l : possible_edges) {
                        // Check if the point is on the line
                        const Line line(to_point(path[l]), to_point(path[l + 1]));
                        if (p == line.a) {
                            front.z() = path[l].z();
                            break;
                        }
                        if (p == line.b) {
                            front.z() = path[l + 1].z();
                            break;
                        }
                        if (point_on_line(p, line)) {
                            front.z() = -(path[l].z() + 1);
                            break;
                        }
                    }
                    if (is_clip(front)) {
                        // Too bad! Couldn't find the src edge, so we just pick the first one and hope it works
                        front.z() = -(path[possible_edges[0]].z() + 1);
                    }
                }
            }

            const ClipperZUtils::ZPoint* previous_src_point;
            if (is_src(front)) {
                // The segment starts with a point from src path, which means apart from the last point,
                // all other points on this segment should come from the src path or the clip polygon

                // Connect the segment to the src path
                auto& node = split_chain[front.z()];
                node.insert(node.begin(), &segment);

                previous_src_point = &front;
            } else if (is_new(front)) {
                const auto id = -front.z() - 1; // Get the src path index
                const ClipperZUtils::ZPoint& src_p = path[id]; // Get the corresponding src point
                const auto dist2 = (front - src_p).block<2, 1>(0,0).squaredNorm(); // Distance between the src point and current point
                // Find the place on the src line that current point should lie on
                auto& node = split_chain[id];
                auto it = std::find_if(node.begin(), node.end(), [dist2, &src_p](const ClipperZUtils::ZPath* p) {
                    const ClipperZUtils::ZPoint& p_front = p->front();
                    if (is_src(p_front)) {
                        return false;
                    }

                    const auto dist2_2 = (p_front - src_p).block<2, 1>(0, 0).squaredNorm();
                    return dist2_2 > dist2;
                });
                // Insert this split
                node.insert(it, &segment);

                previous_src_point = &src_p;
            } else {
                assert(false);
            }

            // Once we figured out the start point, we can then normalize the remaining points on the segment
            for (ClipperZUtils::ZPoint& p : segment) {
                assert(!is_new(p) || p == front || p == segment.back()); // Only the first and last point can be a new intersection
                if (is_src(p)) {
                    previous_src_point = &p;
                } else if (is_clip(p)) {
                    // Treat point from clip polygon as new point
                    p.z() = -(previous_src_point->z() + 1);
                }
            }
        }
    }

    // Now we reconstruct the final path by connecting splits
    SplittedLine result;
    size_t       idx  = 0;
    while (idx < split_chain.size()) {
        const ClipperZUtils::ZPoint& p = path[idx];
        const auto& node = split_chain[idx];
        if (node.empty()) {
            result.emplace_back(to_point(p), false, idx);
            idx++;
        } else {
            if (!is_src(node.front()->front())) {
                result.emplace_back(to_point(p), false, idx);
            }
            for (const auto segment : node) {
                for (const ClipperZUtils::ZPoint& sp : *segment) {
                    assert(!is_clip(sp.z()));
                    result.emplace_back(to_point(sp), true, sp.z());
                }
                result.back().clipped = false; // Mark the end of the clipped line
            }

            // Determine the next start point
            const auto back = result.back().src_idx;
            if (back < 0) {
                idx = -back;
            } else {
                result.pop_back();
                idx = back;
            }
        }
    }

    
#ifdef DEBUG_SPLIT_LINE
    {
        ::Slic3r::SVG svg(debug_out_path("do_split_line_%d_result.svg", dbg_id).c_str(), dbg_bbox);
        svg.draw(clip, "red", 0.5);
        svg.draw_outline(clip, "red");
        for (auto it = result.begin() + 1; it != result.end(); ++it) {
            const auto& a = *(it - 1);
            const auto& b = *it;
            const bool  clipped = a.clipped;
            const Line  l(a.p, b.p);
            svg.draw(l, clipped ? "yellow" : "black");
        }
        svg.Close();
    }
#endif

    if (closed) {
        // Remove last point which was duplicated
        result.pop_back();
    }

    return result;
}

} // Algorithm
} // Slic3r
