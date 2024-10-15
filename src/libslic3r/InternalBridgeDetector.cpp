#include "InternalBridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include <algorithm>

namespace Slic3r {

InternalBridgeDetector::InternalBridgeDetector(
    ExPolygon _internal_bridge, const ExPolygons& _fill_no_overlap, coord_t _spacing) :
    fill_no_overlap(_fill_no_overlap),
    spacing(_spacing)
{
    this->internal_bridge_infill.push_back(std::move(_internal_bridge));
    initialize();
}

//#define INTERNAL_BRIDGE_DETECTOR_DEBUG_TO_SVG

void InternalBridgeDetector::initialize()
{
    Polygons grown = offset(this->internal_bridge_infill, float(this->spacing));
    this->m_anchor_regions = diff_ex(grown, offset(this->fill_no_overlap, 10.f));

#ifdef INTERNAL_BRIDGE_DETECTOR_DEBUG_TO_SVG
    static int irun = 0;
    BoundingBox bbox_svg;

    bbox_svg.merge(get_extents(this->internal_bridge_infill));
    bbox_svg.merge(get_extents(this->fill_no_overlap));
    bbox_svg.merge(get_extents(this->m_anchor_regions));
    {
        std::stringstream stri;
        stri << "InternalBridgeDetector_" << irun << ".svg";
        SVG svg(stri.str(), bbox_svg);
        svg.draw(to_polylines(this->internal_bridge_infill), "blue");
        svg.draw(to_polylines(this->fill_no_overlap), "yellow");
        svg.draw(to_polylines(m_anchor_regions), "red");
        svg.Close();
    }
    ++ irun;
#endif
}

bool InternalBridgeDetector::detect_angle()
{
    if (this->m_anchor_regions.empty())
        return false;

    std::vector<InternalBridgeDirection> candidates;
    std::vector<double> angles = bridge_direction_candidates();
    candidates.reserve(angles.size());
    for (size_t i = 0; i < angles.size(); ++ i)
        candidates.emplace_back(InternalBridgeDirection(angles[i]));

    Polygons clip_area = offset(this->internal_bridge_infill, 0.5f * float(this->spacing));

    bool have_coverage = false;
    for (size_t i_angle = 0; i_angle < candidates.size(); ++ i_angle)
    {
        const double angle = candidates[i_angle].angle;

        Lines lines;
        {
            BoundingBox bbox = get_extents_rotated(this->m_anchor_regions, - angle);
            // Cover the region with line segments.
            lines.reserve((bbox.max(1) - bbox.min(1) + this->spacing) / this->spacing);
            double s = sin(angle);
            double c = cos(angle);

            for (coord_t y = bbox.min(1); y <= bbox.max(1); y += this->spacing)
                lines.push_back(Line(
                    Point((coord_t)round(c * bbox.min(0) - s * y), (coord_t)round(c * y + s * bbox.min(0))),
                    Point((coord_t)round(c * bbox.max(0) - s * y), (coord_t)round(c * y + s * bbox.max(0)))));
        }

        double total_length = 0;
        double anchored_length = 0;
        double max_length = 0;
        {
            Lines clipped_lines = intersection_ln(lines, clip_area);
            for (size_t i = 0; i < clipped_lines.size(); ++i) {
                const Line &line = clipped_lines[i];
                double len = line.length();
                total_length += len;
                if (expolygons_contain(this->m_anchor_regions, line.a) && expolygons_contain(this->m_anchor_regions, line.b)) {
                    // This line could be anchored.
                    anchored_length += len;
                    max_length = std::max(max_length, len);
                }
            }
        }
        if (anchored_length == 0.)
            continue;

        have_coverage = true;

        candidates[i_angle].coverage = anchored_length/total_length;
        candidates[i_angle].max_length = max_length;
    }

    if (! have_coverage)
        return false;

    std::sort(candidates.begin(), candidates.end());
    size_t i_best = 0;
    this->angle = candidates[i_best].angle;
    if (this->angle >= PI)
        this->angle -= PI;

    return true;
}

std::vector<double> InternalBridgeDetector::bridge_direction_candidates() const
{
    std::vector<double> angles;
    for (int i = 0; i <= PI/this->resolution; ++i)
        angles.push_back(i * this->resolution);

    // we also test angles of each bridge contour
    {
        Lines lines = to_lines(this->internal_bridge_infill);
        for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line)
            angles.push_back(line->direction());
    }

    // remove duplicates
    double min_resolution = PI/180.0;
    std::sort(angles.begin(), angles.end());
    for (size_t i = 1; i < angles.size(); ++i) {
        if (Slic3r::Geometry::directions_parallel(angles[i], angles[i-1], min_resolution)) {
            angles.erase(angles.begin() + i);
            --i;
        }
    }

    if (Slic3r::Geometry::directions_parallel(angles.front(), angles.back(), min_resolution))
        angles.pop_back();

    return angles;
}



}