#include <cmath>
#include <algorithm>
#include <vector>
//#include <cstddef>

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"
#include "FillTpmsFK.hpp"

namespace Slic3r {

static double scaled_floor(double x,double scale){
	return std::floor(x/scale)*scale;
}

static Polylines make_waves(double gridZ, double density_adjusted, double line_spacing, double width, double height)
{
    const double scaleFactor = scale_(line_spacing) / density_adjusted;
    const double z = gridZ / scaleFactor;
    const double tolerance = std::min(line_spacing / 2, FillTpmsFK::PatternTolerance) / unscale<double>(scaleFactor);

    Polylines result;

    // Equation: cos(2x)*sin(y)*cos(z) + cos(2y)*sin(z)*cos(x) + cos(2z)*sin(x)*cos(y) = 0
    const double xmin = 0.0;
    const double xmax = width;
    const double ymin = 0.0;
    const double ymax = height;

    // Grid resolution - adjust as needed
    const int grid_resolution = 250;
    const double dx = (xmax - xmin) / grid_resolution;
    const double dy = (ymax - ymin) / grid_resolution;

    auto equation = [z](double x, double y) {
        return std::cos(2.0 * x) * std::sin(y) * std::cos(z)
             + std::cos(2.0 * y) * std::sin(z) * std::cos(x)
             + std::cos(2.0 * z) * std::sin(x) * std::cos(y);
    };

    // Create a grid to track visited points
    std::vector<std::vector<bool>> visited(
        grid_resolution + 1, 
        std::vector<bool>(grid_resolution + 1, false)
    );

    // Directions for contour tracing (8-connected)
    const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};

    // Find starting points for contours
    for (int i = 0; i <= grid_resolution; ++i) {
        for (int j = 0; j <= grid_resolution; ++j) {
            double x = xmin + i * dx;
            double y = ymin + j * dy;

            if (visited[i][j]) continue;

            double val = equation(x, y);
            if (std::abs(val) < tolerance) {
                // Found a point on the contour - start tracing
                Polyline contour;
                int ci = i, cj = j;
                int dir = 0; // Initial direction

                do {
                    double cx = xmin + ci * dx;
                    double cy = ymin + cj * dy;
                    contour.points.emplace_back(cx * scaleFactor, cy * scaleFactor);
                    visited[ci][cj] = true;

                    // Find next direction
                    bool found = false;
                    for (int k = 0; k < 8; ++k) {
                        int nd = (dir + k) % 8;
                        int ni = ci + dx8[nd];
                        int nj = cj + dy8[nd];

                        if (ni < 0 || ni > grid_resolution || nj < 0 || nj > grid_resolution)
                            continue;

                        double nx = xmin + ni * dx;
                        double ny = ymin + nj * dy;
                        double nval = equation(nx, ny);

                        if (std::abs(nval) < tolerance && !visited[ni][nj]) {
                            ci = ni;
                            cj = nj;
                            dir = (nd + 4) % 8; // Reverse direction
                            found = true;
                            break;
                        }
                    }

                    if (!found) break;
                } while (true);

                if (contour.points.size() > 1) {
                    // Simplify the contour
                    contour.simplify(tolerance);
                    result.push_back(contour);
                }
            }
        }
    }

    return result;
}

constexpr double FillTpmsFK::PatternTolerance;

void FillTpmsFK::_fill_surface_single(
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
    double      density_adjusted = std::max(0., params.density * DensityAdjust / params.multiline);
    // Distance between the gyroid waves in scaled coordinates.
    coord_t     distance = coord_t(scale_(this->spacing)  / density_adjusted);

    // align bounding box to a multiple of our grid module
    bb.merge(align_to_grid(bb.min, Point(2*M_PI*distance, 2*M_PI*distance)));

    // generate pattern
    Polylines polylines = make_waves(
        scale_(this->z),
        density_adjusted,
        this->spacing,
        ceil(bb.size()(0) / distance) + 1.,
        ceil(bb.size()(1) / distance) + 1.);

	// shift the polyline to the grid origin
	for (Polyline &pl : polylines)
		pl.translate(bb.min);
	
	    // Apply multiline offset if needed
    multiline_fill(polylines, params, spacing);

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
		if (params.dont_connect())
        	append(polylines_out, chain_polylines(polylines));
        else
            this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

	    // new paths must be rotated back
        if (std::abs(infill_angle) >= EPSILON) {
	        for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++ it)
	        	it->rotate(infill_angle);
	    }
    }
}

} // namespace Slic3r