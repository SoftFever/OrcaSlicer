#include <boost/next_prior.hpp>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Surface_sweep_2_algorithms.h>

#include "libslic3r/Geometry/Voronoi.hpp"
#include "libslic3r/Arachne/utils/VoronoiUtils.hpp"

#include "VoronoiUtilsCgal.hpp"

using VD = Slic3r::Geometry::VoronoiDiagram;

namespace Slic3r::Geometry {

using CGAL_Point   = CGAL::Exact_predicates_exact_constructions_kernel::Point_2;
using CGAL_Segment = CGAL::Arr_segment_traits_2<CGAL::Exact_predicates_exact_constructions_kernel>::Curve_2;

inline static CGAL_Point to_cgal_point(const VD::vertex_type &pt) { return {pt.x(), pt.y()}; }

// FIXME Lukas H.: Also includes parabolic segments.
bool VoronoiUtilsCgal::is_voronoi_diagram_planar_intersection(const VD &voronoi_diagram)
{
    assert(std::all_of(voronoi_diagram.edges().cbegin(), voronoi_diagram.edges().cend(),
                       [](const VD::edge_type &edge) { return edge.color() == 0; }));

    std::vector<CGAL_Segment> segments;
    segments.reserve(voronoi_diagram.num_edges());

    for (const VD::edge_type &edge : voronoi_diagram.edges()) {
        if (edge.color() != 0)
            continue;

        if (edge.is_finite() && edge.is_linear() && edge.vertex0() != nullptr && edge.vertex1() != nullptr &&
            Arachne::VoronoiUtils::is_finite(*edge.vertex0()) && Arachne::VoronoiUtils::is_finite(*edge.vertex1())) {
            segments.emplace_back(to_cgal_point(*edge.vertex0()), to_cgal_point(*edge.vertex1()));
            edge.color(1);
            assert(edge.twin() != nullptr);
            edge.twin()->color(1);
        }
    }

    for (const VD::edge_type &edge : voronoi_diagram.edges())
        edge.color(0);

    std::vector<CGAL_Point> intersections_pt;
    CGAL::compute_intersection_points(segments.begin(), segments.end(), std::back_inserter(intersections_pt));
    return intersections_pt.empty();
}

static bool check_if_three_vectors_are_ccw(const CGAL_Point &common_pt, const CGAL_Point &pt_1, const CGAL_Point &pt_2, const CGAL_Point &test_pt) {
    CGAL::Orientation orientation = CGAL::orientation(common_pt, pt_1, pt_2);
    if (orientation == CGAL::Orientation::COLLINEAR) {
        // The first two edges are collinear, so the third edge must be on the right side on the first of them.
        return CGAL::orientation(common_pt, pt_1, test_pt) == CGAL::Orientation::RIGHT_TURN;
    } else if (orientation == CGAL::Orientation::LEFT_TURN) {
        // CCW oriented angle between vectors (common_pt, pt1) and (common_pt, pt2) is bellow PI.
        // So we need to check if test_pt isn't between them.
        CGAL::Orientation orientation1 = CGAL::orientation(common_pt, pt_1, test_pt);
        CGAL::Orientation orientation2 = CGAL::orientation(common_pt, pt_2, test_pt);
        return (orientation1 != CGAL::Orientation::LEFT_TURN || orientation2 != CGAL::Orientation::RIGHT_TURN);
    } else {
        assert(orientation == CGAL::Orientation::RIGHT_TURN);
        // CCW oriented angle between vectors (common_pt, pt1) and (common_pt, pt2) is upper PI.
        // So we need to check if test_pt is between them.
        CGAL::Orientation orientation1 = CGAL::orientation(common_pt, pt_1, test_pt);
        CGAL::Orientation orientation2 = CGAL::orientation(common_pt, pt_2, test_pt);
        return (orientation1 == CGAL::Orientation::RIGHT_TURN || orientation2 == CGAL::Orientation::LEFT_TURN);
    }
}

bool VoronoiUtilsCgal::is_voronoi_diagram_planar_angle(const VoronoiDiagram &voronoi_diagram)
{
    for (const VD::vertex_type &vertex : voronoi_diagram.vertices()) {
        std::vector<const VD::edge_type *> edges;
        const VD::edge_type               *edge = vertex.incident_edge();

        do {
            // FIXME Lukas H.: Also process parabolic segments.
            if (edge->is_finite() && edge->is_linear() && edge->vertex0() != nullptr && edge->vertex1() != nullptr &&
                Arachne::VoronoiUtils::is_finite(*edge->vertex0()) && Arachne::VoronoiUtils::is_finite(*edge->vertex1()))
                edges.emplace_back(edge);

            edge = edge->rot_next();
        } while (edge != vertex.incident_edge());

        // Checking for CCW make sense for three and more edges.
        if (edges.size() > 2) {
            for (auto edge_it = edges.begin() ; edge_it != edges.end(); ++edge_it) {
                const Geometry::VoronoiDiagram::edge_type *prev_edge = edge_it == edges.begin() ? edges.back() : *std::prev(edge_it);
                const Geometry::VoronoiDiagram::edge_type *curr_edge = *edge_it;
                const Geometry::VoronoiDiagram::edge_type *next_edge = std::next(edge_it) == edges.end() ? edges.front() : *std::next(edge_it);

                if (!check_if_three_vectors_are_ccw(to_cgal_point(*prev_edge->vertex0()), to_cgal_point(*prev_edge->vertex1()),
                                                    to_cgal_point(*curr_edge->vertex1()), to_cgal_point(*next_edge->vertex1())))
                    return false;
            }
        }
    }

    return true;
}


} // namespace Slic3r::Geometry
