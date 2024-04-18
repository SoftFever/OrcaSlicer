//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <stack>
#include <optional>
#include <boost/log/trivial.hpp>

#include "linearAlg2D.hpp"
#include "VoronoiUtils.hpp"

namespace Slic3r::Arachne
{

Vec2i64 VoronoiUtils::p(const vd_t::vertex_type *node)
{
    const double x = node->x();
    const double y = node->y();
    assert(std::isfinite(x) && std::isfinite(y));
    assert(x <= double(std::numeric_limits<int64_t>::max()) && x >= std::numeric_limits<int64_t>::lowest());
    assert(y <= double(std::numeric_limits<int64_t>::max()) && y >= std::numeric_limits<int64_t>::lowest());
    return {int64_t(x + 0.5 - (x < 0)), int64_t(y + 0.5 - (y < 0))}; // Round to the nearest integer coordinates.
}

Point VoronoiUtils::getSourcePoint(const vd_t::cell_type& cell, const std::vector<Segment>& segments)
{
    assert(cell.contains_point());
    if(!cell.contains_point())
        BOOST_LOG_TRIVIAL(debug) << "Voronoi cell doesn't contain a source point!";

    switch (cell.source_category()) {
        case boost::polygon::SOURCE_CATEGORY_SINGLE_POINT:
            assert(false && "Voronoi diagram is always constructed using segments, so cell.source_category() shouldn't be SOURCE_CATEGORY_SINGLE_POINT!\n");
            BOOST_LOG_TRIVIAL(error) << "Voronoi diagram is always constructed using segments, so cell.source_category() shouldn't be SOURCE_CATEGORY_SINGLE_POINT!";
            break;
        case boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT:
            assert(cell.source_index() < segments.size());
            return segments[cell.source_index()].to();
            break;
        case boost::polygon::SOURCE_CATEGORY_SEGMENT_END_POINT:
            assert(cell.source_index() < segments.size());
            return segments[cell.source_index()].from();
            break;
        default:
            assert(false && "getSourcePoint should only be called on point cells!\n");
            break;
    }

    assert(false && "cell.source_category() is equal to an invalid value!\n");
    BOOST_LOG_TRIVIAL(error) << "cell.source_category() is equal to an invalid value!";
    return {};
}

PolygonsPointIndex VoronoiUtils::getSourcePointIndex(const vd_t::cell_type& cell, const std::vector<Segment>& segments)
{
    assert(cell.contains_point());
    if(!cell.contains_point())
        BOOST_LOG_TRIVIAL(debug) << "Voronoi cell doesn't contain a source point!";

    assert(cell.source_category() != boost::polygon::SOURCE_CATEGORY_SINGLE_POINT);
    switch (cell.source_category()) {
        case boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT: {
            assert(cell.source_index() < segments.size());
            PolygonsPointIndex ret = segments[cell.source_index()];
            ++ret;
            return ret;
            break;
        }
        case boost::polygon::SOURCE_CATEGORY_SEGMENT_END_POINT: {
            assert(cell.source_index() < segments.size());
            return segments[cell.source_index()];
            break;
        }
        default:
            assert(false && "getSourcePoint should only be called on point cells!\n");
            break;
    }
    PolygonsPointIndex ret = segments[cell.source_index()];
    return ++ret;
}

const VoronoiUtils::Segment &VoronoiUtils::getSourceSegment(const vd_t::cell_type &cell, const std::vector<Segment> &segments)
{
    assert(cell.contains_segment());
    if (!cell.contains_segment())
        BOOST_LOG_TRIVIAL(debug) << "Voronoi cell doesn't contain a source segment!";

    return segments[cell.source_index()];
}

class PointMatrix
{
public:
    double matrix[4];

    PointMatrix()
    {
        matrix[0] = 1;
        matrix[1] = 0;
        matrix[2] = 0;
        matrix[3] = 1;
    }

    PointMatrix(double rotation)
    {
        rotation = rotation / 180 * M_PI;
        matrix[0] = cos(rotation);
        matrix[1] = -sin(rotation);
        matrix[2] = -matrix[1];
        matrix[3] = matrix[0];
    }

    PointMatrix(const Point p)
    {
        matrix[0] = p.x();
        matrix[1] = p.y();
        double f = sqrt((matrix[0] * matrix[0]) + (matrix[1] * matrix[1]));
        matrix[0] /= f;
        matrix[1] /= f;
        matrix[2] = -matrix[1];
        matrix[3] = matrix[0];
    }

    static PointMatrix scale(double s)
    {
        PointMatrix ret;
        ret.matrix[0] = s;
        ret.matrix[3] = s;
        return ret;
    }

    Point apply(const Point p) const
    {
        return Point(coord_t(p.x() * matrix[0] + p.y() * matrix[1]), coord_t(p.x() * matrix[2] + p.y() * matrix[3]));
    }

    Point unapply(const Point p) const
    {
        return Point(coord_t(p.x() * matrix[0] + p.y() * matrix[2]), coord_t(p.x() * matrix[1] + p.y() * matrix[3]));
    }
};
std::vector<Point> VoronoiUtils::discretizeParabola(const Point& p, const Segment& segment, Point s, Point e, coord_t approximate_step_size, float transitioning_angle)
{
    std::vector<Point> discretized;
    // x is distance of point projected on the segment ab
    // xx is point projected on the segment ab
    const Point a = segment.from();
    const Point b = segment.to();
    const Point ab = b - a;
    const Point as = s - a;
    const Point ae = e - a;
    const coord_t ab_size = ab.cast<int64_t>().norm();
    const coord_t sx = as.cast<int64_t>().dot(ab.cast<int64_t>()) / ab_size;
    const coord_t ex = ae.cast<int64_t>().dot(ab.cast<int64_t>()) / ab_size;
    const coord_t sxex = ex - sx;

    assert((as.cast<int64_t>().dot(ab.cast<int64_t>()) / int64_t(ab_size)) <= std::numeric_limits<coord_t>::max());
    assert((ae.cast<int64_t>().dot(ab.cast<int64_t>()) / int64_t(ab_size)) <= std::numeric_limits<coord_t>::max());

    const Point ap = p - a;
    const coord_t px = ap.cast<int64_t>().dot(ab.cast<int64_t>()) / ab_size;

    assert((ap.cast<int64_t>().dot(ab.cast<int64_t>()) / int64_t(ab_size)) <= std::numeric_limits<coord_t>::max());

    Point pxx;
    Line(a, b).distance_to_infinite_squared(p, &pxx);
    const Point ppxx = pxx - p;
    const coord_t d = ppxx.cast<int64_t>().norm();
    const PointMatrix rot = PointMatrix(ppxx.rotate_90_degree_ccw());

    if (d == 0)
    {
        discretized.emplace_back(s);
        discretized.emplace_back(e);
        return discretized;
    }
    
    const float marking_bound = atan(transitioning_angle * 0.5);
    int64_t msx = - marking_bound * int64_t(d); // projected marking_start
    int64_t mex = marking_bound * int64_t(d); // projected marking_end

    assert(msx <= std::numeric_limits<coord_t>::max());
    assert(double(msx) * double(msx) <= double(std::numeric_limits<int64_t>::max()));
    assert(mex <= std::numeric_limits<coord_t>::max());
    assert(double(msx) * double(msx) / double(2 * d) + double(d / 2) <= std::numeric_limits<coord_t>::max());

    const coord_t marking_start_end_h = msx * msx / (2 * d) + d / 2;
    Point marking_start = rot.unapply(Point(coord_t(msx), marking_start_end_h)) + pxx;
    Point marking_end = rot.unapply(Point(coord_t(mex), marking_start_end_h)) + pxx;
    const int dir = (sx > ex) ? -1 : 1;
    if (dir < 0)
    {
        std::swap(marking_start, marking_end);
        std::swap(msx, mex);
    }
    
    bool add_marking_start = msx * int64_t(dir) > int64_t(sx - px) * int64_t(dir) && msx * int64_t(dir) < int64_t(ex - px) * int64_t(dir);
    bool add_marking_end = mex * int64_t(dir) > int64_t(sx - px) * int64_t(dir) && mex * int64_t(dir) < int64_t(ex - px) * int64_t(dir);

    const Point apex = rot.unapply(Point(0, d / 2)) + pxx;
    bool add_apex = int64_t(sx - px) * int64_t(dir) < 0 && int64_t(ex - px) * int64_t(dir) > 0;

    assert(!(add_marking_start && add_marking_end) || add_apex);
    if(add_marking_start && add_marking_end && !add_apex)
    {
        BOOST_LOG_TRIVIAL(warning) << "Failing to discretize parabola! Must add an apex or one of the endpoints.";
    }
    
    const coord_t step_count = static_cast<coord_t>(static_cast<float>(std::abs(ex - sx)) / approximate_step_size + 0.5);
    
    discretized.emplace_back(s);
    for (coord_t step = 1; step < step_count; step++)
    {
        assert(double(sxex) * double(step) <= double(std::numeric_limits<int64_t>::max()));
        const int64_t x = int64_t(sx) + int64_t(sxex) * int64_t(step) / int64_t(step_count) - int64_t(px);
        assert(double(x) * double(x) <= double(std::numeric_limits<int64_t>::max()));
        assert(double(x) * double(x) / double(2 * d) + double(d / 2) <= double(std::numeric_limits<int64_t>::max()));
        const int64_t y = int64_t(x) * int64_t(x) / int64_t(2 * d) + int64_t(d / 2);
        
        if (add_marking_start && msx * int64_t(dir) < int64_t(x) * int64_t(dir))
        {
            discretized.emplace_back(marking_start);
            add_marking_start = false;
        }
        if (add_apex && int64_t(x) * int64_t(dir) > 0)
        {
            discretized.emplace_back(apex);
            add_apex = false; // only add the apex just before the 
        }
        if (add_marking_end && mex * int64_t(dir) < int64_t(x) * int64_t(dir))
        {
            discretized.emplace_back(marking_end);
            add_marking_end = false;
        }
        assert(x <= std::numeric_limits<coord_t>::max() && x >= std::numeric_limits<coord_t>::lowest());
        assert(y <= std::numeric_limits<coord_t>::max() && y >= std::numeric_limits<coord_t>::lowest());
        const Point result = rot.unapply(Point(x, y)) + pxx;
        discretized.emplace_back(result);
    }
    if (add_apex)
    {
        discretized.emplace_back(apex);
    }
    if (add_marking_end)
    {
        discretized.emplace_back(marking_end);
    }
    discretized.emplace_back(e);
    return discretized;
}

}//namespace Slic3r::Arachne
