//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.


#ifndef UTILS_VORONOI_UTILS_H
#define UTILS_VORONOI_UTILS_H

#include <vector>


#include <boost/polygon/voronoi.hpp>

#include "PolygonsSegmentIndex.hpp"

namespace Slic3r::Arachne
{

/*!
 */
class VoronoiUtils
{
public:
    using Segment        = PolygonsSegmentIndex;
    using voronoi_data_t = double;
    using vd_t           = boost::polygon::voronoi_diagram<voronoi_data_t>;

    static Point              getSourcePoint(const vd_t::cell_type &cell, const std::vector<Segment> &segments);
    static const Segment     &getSourceSegment(const vd_t::cell_type &cell, const std::vector<Segment> &segments);
    static PolygonsPointIndex getSourcePointIndex(const vd_t::cell_type &cell, const std::vector<Segment> &segments);

    static Vec2i64 p(const vd_t::vertex_type *node);

    /*!
     * Discretize a parabola based on (approximate) step size.
     * The \p approximate_step_size is measured parallel to the \p source_segment, not along the parabola.
     */
    static std::vector<Point> discretizeParabola(const Point &source_point, const Segment &source_segment, Point start, Point end, coord_t approximate_step_size, float transitioning_angle);

    static inline bool is_finite(const VoronoiUtils::vd_t::vertex_type &vertex)
    {
        return std::isfinite(vertex.x()) && std::isfinite(vertex.y());
    }
};

} // namespace Slic3r::Arachne

#endif // UTILS_VORONOI_UTILS_H
