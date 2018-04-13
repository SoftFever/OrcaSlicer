#include "../ClipperUtils.hpp"
#include "../PolylineCollection.hpp"
#include "../Surface.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

#include "FillGyroid.hpp"

namespace Slic3r {

static inline double f(double x, double z_sin, double z_cos, bool vertical, bool flip)
{
    if (vertical) {
        double phase_offset = (z_cos < 0 ? M_PI : 0) + M_PI;
        double a   = sin(x + phase_offset);
        double b   = - z_cos;
        double res = z_sin * cos(x + phase_offset + (flip ? M_PI : 0.));
        double r   = sqrt(sqr(a) + sqr(b));
        return asin(a/r) + asin(res/r) + M_PI;
    }
    else {
        double phase_offset = z_sin < 0 ? M_PI : 0.;
        double a   = cos(x + phase_offset);
        double b   = - z_sin;
        double res = z_cos * sin(x + phase_offset + (flip ? 0 : M_PI));
        double r   = sqrt(sqr(a) + sqr(b));
        return (asin(a/r) + asin(res/r) + 0.5 * M_PI);
    }
}

static inline Polyline make_wave(
    const std::vector<Pointf>& one_period, double width, double height, double offset, double scaleFactor,
    double z_cos, double z_sin, bool vertical)
{
    std::vector<Pointf> points = one_period;
    double period = points.back().x;
    points.pop_back();
    int n = points.size();
    do {
        points.emplace_back(Pointf(points[points.size()-n].x + period, points[points.size()-n].y));
    } while (points.back().x < width);
    points.back().x = width;

    // and construct the final polyline to return:
    Polyline polyline;
    for (auto& point : points) {
        point.y += offset;
        point.y = clamp(0., height, double(point.y));
        if (vertical)
            std::swap(point.x, point.y);
        polyline.points.emplace_back(convert_to<Point>(point * scaleFactor));
    }

    return polyline;
}

static std::vector<Pointf> make_one_period(double width, double scaleFactor, double z_cos, double z_sin, bool vertical, bool flip)
{
    std::vector<Pointf> points;
    double dx = M_PI_4; // very coarse spacing to begin with
    double limit = std::min(2*M_PI, width);
    for (double x = 0.; x < limit + EPSILON; x += dx) {  // so the last point is there too
        x = std::min(x, limit);
        points.emplace_back(Pointf(x,f(x, z_sin,z_cos, vertical, flip)));
    }

    // now we will check all internal points and in case some are too far from the line connecting its neighbours,
    // we will add one more point on each side:
    const double tolerance = .1;
    for (unsigned int i=1;i<points.size()-1;++i) {
        auto& lp = points[i-1]; // left point
        auto& tp = points[i];   // this point
        auto& rp = points[i+1]; // right point
        // calculate distance of the point to the line:
        double dist_mm = unscale(scaleFactor * std::abs( (rp.y - lp.y)*tp.x + (lp.x - rp.x)*tp.y + (rp.x*lp.y - rp.y*lp.x) ) / std::hypot((rp.y - lp.y),(lp.x - rp.x)));

        if (dist_mm > tolerance) {                               // if the difference from straight line is more than this
            double x = 0.5f * (points[i-1].x + points[i].x);
            points.emplace_back(Pointf(x, f(x, z_sin, z_cos, vertical, flip)));
            x = 0.5f * (points[i+1].x + points[i].x);
            points.emplace_back(Pointf(x, f(x, z_sin, z_cos, vertical, flip)));
            std::sort(points.begin(), points.end());            // we added the points to the end, but need them all in order
            --i;                                                // decrement i so we also check the first newly added point
        }
    }
    return points;
}

static Polylines make_gyroid_waves(double gridZ, double density_adjusted, double line_spacing, double width, double height)
{
    const double scaleFactor = scale_(line_spacing) / density_adjusted;
 //scale factor for 5% : 8 712 388
 // 1z = 10^-6 mm ?
    const double z     = gridZ / scaleFactor;
    const double z_sin = sin(z);
    const double z_cos = cos(z);

    bool vertical = (std::abs(z_sin) <= std::abs(z_cos));
    double lower_bound = 0.;
    double upper_bound = height;
    bool flip = true;
    if (vertical) {
        flip = false;
        lower_bound = -M_PI;
        upper_bound = width - M_PI_2;
        std::swap(width,height);
    }

    std::vector<Pointf> one_period = make_one_period(width, scaleFactor, z_cos, z_sin, vertical, flip); // creates one period of the waves, so it doesn't have to be recalculated all the time
    Polylines result;

    for (double y0 = lower_bound; y0 < upper_bound+EPSILON; y0 += 2*M_PI)           // creates odd polylines
            result.emplace_back(make_wave(one_period, width, height, y0, scaleFactor, z_cos, z_sin, vertical));

    flip = !flip;                                                                   // even polylines are a bit shifted
    one_period = make_one_period(width, scaleFactor, z_cos, z_sin, vertical, flip); // updates the one period sample
    for (double y0 = lower_bound + M_PI; y0 < upper_bound+EPSILON; y0 += 2*M_PI)    // creates even polylines
            result.emplace_back(make_wave(one_period, width, height, y0, scaleFactor, z_cos, z_sin, vertical));

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
    double      density_adjusted = std::max(0., params.density * 2.);
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
