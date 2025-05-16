#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"
#include <cmath>
#include <algorithm>


#include "FillGyroid.hpp"

namespace Slic3r {

static inline double f(double x, double z_sin, double z_cos, bool vertical, bool flip)
{
    if (vertical) {
        double phase_offset = (z_cos < 0 ? M_PI : 0) + M_PI;
        double a   = sin(x + phase_offset);
        double b   = - z_cos;
        double res = z_sin * cos(x + phase_offset + (flip ? M_PI : 0.));
        double r   = std::hypot(a, b);
        return asin(a/r) + asin(res/r) + M_PI;
    }
    else {
        double phase_offset = z_sin < 0 ? M_PI : 0.;
        double a   = cos(x + phase_offset);
        double b   = - z_sin;
        double res = z_cos * sin(x + phase_offset + (flip ? 0 : M_PI));
        double r   = std::hypot(a,b);
        return (asin(a/r) + asin(res/r) + M_PI_2);
    }
}
static inline double f2(double x, double z_sin, double z_cos, bool vertical, bool flip, double offset){
   
    if (offset == 0.0) {
        return f(x, z_sin, z_cos, vertical, flip);
    }
    const double h = 0.2; // numerical derivative step size

    // Numeric derivative
    double f_plus  = f(x + h, z_sin, z_cos, vertical, flip);
    double f_minus = f(x - h, z_sin, z_cos, vertical, flip);
    double df_dx   = (f_plus - f_minus) / (2 * h); // Slope at x

    df_dx = std::clamp(df_dx, -1e5, 1e5);

    double cos_alpha = 1.0 / std::hypot(1.0, df_dx); 

    if (fabs(cos_alpha) < 1e-10) {
        cos_alpha = 1e-10; // avoid division by zero
    }

    double fx       = f(x, z_sin, z_cos, vertical, flip);
    double y_offset = fx + offset / cos_alpha;

    return y_offset;
}

static inline Polyline make_wave(
    const std::vector<Vec2d>& one_period, double width, double height, double offset, double scaleFactor,
    double z_cos, double z_sin, bool vertical, bool flip,double multiline_offset)
{
    std::vector<Vec2d> points = one_period;
    double period = points.back()(0);
    if (width != period) // do not extend if already truncated
    {
        points.reserve(one_period.size() * size_t(floor(width / period)));
        points.pop_back();

        size_t n = points.size();
        do {
            points.emplace_back(points[points.size() - n].x() + period, points[points.size() - n].y());
        } while (points.back()(0) < width - EPSILON);

        points.emplace_back(Vec2d(width, f2(width, z_sin, z_cos, vertical, flip, multiline_offset)));
    }

    // and construct the final polyline to return:
    Polyline polyline;
    polyline.points.reserve(points.size());
    for (auto& point : points) {
        point(1) += offset;
        point(1) = std::clamp(double(point.y()), 0., height);
        if (vertical)
            std::swap(point(0), point(1));
        polyline.points.emplace_back((point * scaleFactor).cast<coord_t>());
    }

    return polyline;
}

static std::vector<Vec2d> make_one_period(
    double width, double scaleFactor, double z_cos, double z_sin, bool vertical, bool flip, double tolerance, double multiline_offset)
{
    std::vector<Vec2d> points;
    double dx    = M_PI_2; // exact coordinates on main inflexion lobes
    double limit = std::min(2 * M_PI, width);
    points.reserve(coord_t(ceil(limit / tolerance / 3)));

    for (double x = 0.; x < limit - EPSILON; x += dx) {
        points.emplace_back(x, f2(x, z_sin, z_cos, vertical, flip, multiline_offset));
    }
    points.emplace_back(limit, f2(limit, z_sin, z_cos, vertical, flip, multiline_offset));

    // piecewise increase in resolution up to requested tolerance
    for(;;)
    {
        size_t size = points.size();
        for (unsigned int i = 1; i < size; ++i) {
            auto& lp = points[i - 1]; // left point
            auto& rp = points[i];  // right point
            double x = lp(0) + (rp(0) - lp(0)) / 2;
            double y = f2(x, z_sin, z_cos, vertical, flip,multiline_offset);
            Vec2d ip = {x, y};
            if (std::abs(cross2(Vec2d(ip - lp), Vec2d(ip - rp))) > sqr(tolerance)) {
                points.emplace_back(std::move(ip));
            }
        }

        if (size == points.size())
            break;
        else
        {
            // insert new points in order
            std::sort(points.begin(), points.end(),
                      [](const Vec2d &lhs, const Vec2d &rhs) { return lhs(0) < rhs(0); });
        }
    }

    return points;
}

static Polylines make_gyroid_waves(double gridZ, double density_adjusted, double line_spacing, double width, double height, unsigned int multiline, double line_width)
{
    const double scaleFactor = scale_(line_spacing) / density_adjusted;

    // tolerance in scaled units. clamp the maximum tolerance as there's
    // no processing-speed benefit to do so beyond a certain point
    const double tolerance = std::min(line_spacing / 2, FillGyroid::PatternTolerance) / unscale<double>(scaleFactor);

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
        std::swap(width, height);
    }

    Polylines    result;
    const double offset_step = line_width / unscale<double>(scaleFactor);

    // compute symmetric offsets around the center
    std::vector<double> offsets;
    offsets.reserve(multiline);
    if (multiline % 2 == 0) {
        // Even: no center line, symmetric offsets around 0
        for (unsigned int i = 0; i < multiline / 2; ++i) {
            double val = (i + 0.5) * offset_step;
            offsets.push_back(-val);
            offsets.push_back(+val);
        }
    } else {
        // Odd: center line at offset 0
        offsets.push_back(0.0);
        for (unsigned int i = 1; i <= multiline / 2; ++i) {
            double val = i * offset_step;
            offsets.push_back(-val);
            offsets.push_back(+val);
        }
    }

    for (double y0 = lower_bound; y0 < upper_bound + EPSILON; y0 += M_PI) {
        // creates odd polylines
        for (double offset : offsets) {
            result.emplace_back(make_wave(make_one_period(width, scaleFactor, z_cos, z_sin, vertical, flip, tolerance, offset), width,// creates one period of the waves, so it doesn't have to be recalculated all the time
                                          height, y0, scaleFactor, z_cos, z_sin, vertical, flip, offset));
        }
        // creates even polylines
        y0 += M_PI;
        if (y0 < upper_bound + EPSILON) {
            bool local_flip = !flip;
            for (double offset : offsets) {
                result.emplace_back(make_wave(make_one_period(width, scaleFactor, z_cos, z_sin, vertical, local_flip, tolerance, offset),
                                              width, height, y0, scaleFactor, z_cos, z_sin, vertical, local_flip, offset));
            }
        }
    }

    return result;
}

// FIXME: needed to fix build on Mac on buildserver
constexpr double FillGyroid::PatternTolerance;

void FillGyroid::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon, 
    Polylines                       &polylines_out)
{
    auto infill_angle = float(this->angle + (CorrectionAngle * 2*M_PI) / 360.);
    if(std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    BoundingBox bb = expolygon.contour.bounding_box();
    // Density adjusted to have a good %of weight.
    double density_adjusted = std::max(0., params.density * DensityAdjust / params.multiline);
    // Distance between the gyroid waves in scaled coordinates.
    coord_t distance = coord_t(scale_(this->spacing) / density_adjusted);

    // align bounding box to a multiple of our grid module
    bb.merge(align_to_grid(bb.min, Point(2 * M_PI * distance, 2 * M_PI * distance)));

    // generate pattern
    Polylines polylines = make_gyroid_waves(
        scale_(this->z),
        density_adjusted,
        this->spacing,
        ceil(bb.size()(0) / distance) + 1.,
        ceil(bb.size()(1) / distance) + 1.,											
		params.multiline,
        this->spacing);				

	// shift the polyline to the grid origin
	for (Polyline &pl : polylines)
		pl.translate(bb.min);

	polylines = intersection_pl(polylines, expolygon);

    if (! polylines.empty()) {
		// Remove very small bits, but be careful to not remove infill lines connecting thin walls!
        // The infill perimeter lines should be separated by around a single infill line width.
        const double minlength = scale_(0.8 * this->spacing);
		polylines.erase(
			std::remove_if(polylines.begin(), polylines.end(), [minlength](const Polyline &pl) { return pl.length() < minlength; }),
			polylines.end());
    }

	if (! polylines.empty()) {
		// connect lines
		size_t polylines_out_first_idx = polylines_out.size();
        chain_or_connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

	    // new paths must be rotated back
        if (std::abs(infill_angle) >= EPSILON) {
	        for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++ it)
	        	it->rotate(infill_angle);
	    }
    }
}

} // namespace Slic3r