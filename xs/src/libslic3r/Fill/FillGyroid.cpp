#include "../ClipperUtils.hpp"
#include "../PolylineCollection.hpp"
#include "../Surface.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

#include "FillGyroid.hpp"

namespace Slic3r {

static inline Polyline make_wave_vertical(
    double width, double height, double x0,
    double segmentSize, double scaleFactor,
    double z_cos, double z_sin, bool flip)
{
    Polyline polyline;
    polyline.points.emplace_back(Point(coord_t(clamp(0., width, x0) * scaleFactor), 0));
    double phase_offset_sin = (z_cos < 0 ? M_PI : 0) + M_PI;
    double phase_offset_cos = (z_cos < 0 ? M_PI : 0) + M_PI + (flip ? M_PI : 0.);
    for (double y = 0.; y < height + segmentSize; y += segmentSize) {
        y = std::min(y, height);
        double a   = sin(y + phase_offset_sin);
        double b   = - z_cos;
        double res = z_sin * cos(y + phase_offset_cos);
        double r   = sqrt(sqr(a) + sqr(b));
        double x   = clamp(0., width, asin(a/r) + asin(res/r) + M_PI + x0);
        polyline.points.emplace_back(convert_to<Point>(Pointf(x, y) * scaleFactor));
    }
    if (flip)
        std::reverse(polyline.points.begin(), polyline.points.end());
    return polyline;
}

static inline Polyline make_wave_horizontal(
    double width, double height, double y0, 
    double segmentSize, double scaleFactor,
    double z_cos, double z_sin, bool flip)
{
    Polyline polyline;
    polyline.points.emplace_back(Point(0, coord_t(clamp(0., height, y0) * scaleFactor)));
    double phase_offset_sin = (z_sin < 0 ? M_PI : 0) + (flip ? 0 : M_PI);
    double phase_offset_cos = z_sin < 0 ? M_PI : 0.;
    for (double x = 0.; x < width + segmentSize; x += segmentSize) {
        x = std::min(x, width);
        double a   = cos(x + phase_offset_cos);
        double b   = - z_sin;
        double res = z_cos * sin(x + phase_offset_sin);
        double r   = sqrt(sqr(a) + sqr(b));
        double y   = clamp(0., height, asin(a/r) + asin(res/r) + 0.5 * M_PI + y0);
        polyline.points.emplace_back(convert_to<Point>(Pointf(x, y) * scaleFactor));
    }
    if (flip)
        std::reverse(polyline.points.begin(), polyline.points.end());
    return polyline;
}

static Polylines make_gyroid_waves(double gridZ, double density_adjusted, double line_spacing, double width, double height)
{
    double scaleFactor = scale_(line_spacing) / density_adjusted;
    double segmentSize = 0.5 * density_adjusted;
 //scale factor for 5% : 8 712 388
 // 1z = 10^-6 mm ?
    double z     = gridZ / scaleFactor;
    double z_sin = sin(z);
    double z_cos = cos(z);
    Polylines result;
    if (abs(z_sin) <= abs(z_cos)) {
        // Vertical wave
        double x0 = M_PI * (int)((- 0.5 * M_PI) / M_PI - 1.);
        bool   flip          = ((int)(x0 / M_PI + 1.) & 1) != 0;
        for (; x0 < width - 0.5 * M_PI; x0 += M_PI, flip = ! flip)
            result.emplace_back(make_wave_vertical(width, height, x0, segmentSize, scaleFactor, z_cos, z_sin, flip));
    } else {
        // Horizontal wave
        bool flip = true;
        for (double y0 = 0.; y0 < height; y0 += M_PI, flip = !flip)
            result.emplace_back(make_wave_horizontal(width, height, y0, segmentSize, scaleFactor, z_cos, z_sin, flip));
    }
    return result;
}

void FillGyroid::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                       &expolygon, 
    Polylines                       &polylines_out)
{
    // no rotation is supported for this infill pattern (yet)
    BoundingBox bb = expolygon.contour.bounding_box();
    // Density adjusted to have a good %of weight.
    double      density_adjusted = params.density * 1.75;
    // Distance between the gyroid waves in scaled coordinates.
    coord_t     distance = coord_t(scale_(this->spacing) / density_adjusted);

    // align bounding box to a multiple of our grid module
    bb.merge(_align_to_grid(bb.min, Point(2.*M_PI*distance, 2.*M_PI*distance)));
    
    // generate pattern
    Polylines   polylines = make_gyroid_waves(
        scale_(this->z),
        density_adjusted,
        this->spacing,
        ceil(bb.size().x / distance) + 1.,
        ceil(bb.size().y / distance) + 1.);
    
    // move pattern in place
    for (Polyline &polyline : polylines)
        polyline.translate(bb.min.x, bb.min.y);

    // clip pattern to boundaries
    polylines = intersection_pl(polylines, (Polygons)expolygon);

    // connect lines
    if (! params.dont_connect && ! polylines.empty()) { // prevent calling leftmost_point() on empty collections
        ExPolygon expolygon_off;
        {
            ExPolygons expolygons_off = offset_ex(expolygon, (float)SCALED_EPSILON);
            if (! expolygons_off.empty()) {
                // When expanding a polygon, the number of islands could only shrink. Therefore the offset_ex shall generate exactly one expanded island for one input island.
                assert(expolygons_off.size() == 1);
                std::swap(expolygon_off, expolygons_off.front());
            }
        }
        Polylines chained = PolylineCollection::chained_path_from(
            std::move(polylines), 
            PolylineCollection::leftmost_point(polylines), false); // reverse allowed
        bool first = true;
        for (Polyline &polyline : chained) {
            if (! first) {
                // Try to connect the lines.
                Points &pts_end = polylines_out.back().points;
                const Point &first_point = polyline.points.front();
                const Point &last_point = pts_end.back();
                // TODO: we should also check that both points are on a fill_boundary to avoid 
                // connecting paths on the boundaries of internal regions
                // TODO: avoid crossing current infill path
                if (first_point.distance_to(last_point) <= 5 * distance && 
                    expolygon_off.contains(Line(last_point, first_point))) {
                    // Append the polyline.
                    pts_end.insert(pts_end.end(), polyline.points.begin(), polyline.points.end());
                    continue;
                }
            }
            // The lines cannot be connected.
            polylines_out.emplace_back(std::move(polyline));
            first = false;
        }
    }
}

} // namespace Slic3r
