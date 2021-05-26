#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"

#include "Fill3DHoneycomb.hpp"

namespace Slic3r {

/*
Creates a contiguous sequence of points at a specified height that make
up a horizontal slice of the edges of a space filling truncated
octahedron tesselation. The octahedrons are oriented so that the
square faces are in the horizontal plane with edges parallel to the X
and Y axes.

Credits: David Eccles (gringer).
*/

// Generate an array of points that are in the same direction as the
// basic printing line (i.e. Y points for columns, X points for rows)
// Note: a negative offset only causes a change in the perpendicular
// direction
static std::vector<coordf_t> colinearPoints(const coordf_t offset, const size_t baseLocation, size_t gridLength)
{
    const coordf_t offset2 = std::abs(offset / coordf_t(2.));
    std::vector<coordf_t> points;
    points.push_back(baseLocation - offset2);
    for (size_t i = 0; i < gridLength; ++i) {
        points.push_back(baseLocation + i + offset2);
        points.push_back(baseLocation + i + 1 - offset2);
    }
    points.push_back(baseLocation + gridLength + offset2);
    return points;
}

// Generate an array of points for the dimension that is perpendicular to
// the basic printing line (i.e. X points for columns, Y points for rows)
static std::vector<coordf_t> perpendPoints(const coordf_t offset, const size_t baseLocation, size_t gridLength)
{
    coordf_t offset2 = offset / coordf_t(2.);
    coord_t  side    = 2 * (baseLocation & 1) - 1;
    std::vector<coordf_t> points;
    points.push_back(baseLocation - offset2 * side);
    for (size_t i = 0; i < gridLength; ++i) {
        side = 2*((i+baseLocation) & 1) - 1;
        points.push_back(baseLocation + offset2 * side);
        points.push_back(baseLocation + offset2 * side);
    }
    points.push_back(baseLocation - offset2 * side);
    return points;
}

// Trims an array of points to specified rectangular limits. Point
// components that are outside these limits are set to the limits.
static inline void trim(Pointfs &pts, coordf_t minX, coordf_t minY, coordf_t maxX, coordf_t maxY)
{
    for (Vec2d &pt : pts) {
        pt.x() = std::clamp(pt.x(), minX, maxX);
        pt.y() = std::clamp(pt.y(), minY, maxY);
    }
}

static inline Pointfs zip(const std::vector<coordf_t> &x, const std::vector<coordf_t> &y)
{
    assert(x.size() == y.size());
    Pointfs out;
    out.reserve(x.size());
    for (size_t i = 0; i < x.size(); ++ i)
        out.push_back(Vec2d(x[i], y[i]));
    return out;
}

// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron with edge length 1.
// curveType specifies which lines to print, 1 for vertical lines
// (columns), 2 for horizontal lines (rows), and 3 for both.
static std::vector<Pointfs> makeNormalisedGrid(coordf_t z, size_t gridWidth, size_t gridHeight, size_t curveType)
{
    // offset required to create a regular octagram
    coordf_t octagramGap = coordf_t(0.5);
    
    // sawtooth wave function for range f($z) = [-$octagramGap .. $octagramGap]
    coordf_t a = std::sqrt(coordf_t(2.));  // period
    coordf_t wave = fabs(fmod(z, a) - a/2.)/a*4. - 1.;
    coordf_t offset = wave * octagramGap;
    
    std::vector<Pointfs> points;
    if ((curveType & 1) != 0) {
        for (size_t x = 0; x <= gridWidth; ++x) {
            points.push_back(Pointfs());
            Pointfs &newPoints = points.back();
            newPoints = zip(
                perpendPoints(offset, x, gridHeight), 
                colinearPoints(offset, 0, gridHeight));
            // trim points to grid edges
            trim(newPoints, coordf_t(0.), coordf_t(0.), coordf_t(gridWidth), coordf_t(gridHeight));
            if (x & 1)
                std::reverse(newPoints.begin(), newPoints.end());
        }
    }
    if ((curveType & 2) != 0) {
        for (size_t y = 0; y <= gridHeight; ++y) {
            points.push_back(Pointfs());
            Pointfs &newPoints = points.back();
            newPoints = zip(
                colinearPoints(offset, 0, gridWidth),
                perpendPoints(offset, y, gridWidth));
            // trim points to grid edges
            trim(newPoints, coordf_t(0.), coordf_t(0.), coordf_t(gridWidth), coordf_t(gridHeight));
            if (y & 1)
                std::reverse(newPoints.begin(), newPoints.end());
        }
    }
    return points;
}

// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron with a specified
// grid square size.
static Polylines makeGrid(coord_t z, coord_t gridSize, size_t gridWidth, size_t gridHeight, size_t curveType)
{
    coord_t  scaleFactor = gridSize;
    coordf_t normalisedZ = coordf_t(z) / coordf_t(scaleFactor);
    std::vector<Pointfs> polylines = makeNormalisedGrid(normalisedZ, gridWidth, gridHeight, curveType);
    Polylines result;
    result.reserve(polylines.size());
    for (std::vector<Pointfs>::const_iterator it_polylines = polylines.begin(); it_polylines != polylines.end(); ++ it_polylines) {
        result.push_back(Polyline());
        Polyline &polyline = result.back();
        for (Pointfs::const_iterator it = it_polylines->begin(); it != it_polylines->end(); ++ it)
            polyline.points.push_back(Point(coord_t((*it)(0) * scaleFactor), coord_t((*it)(1) * scaleFactor)));
    }
    return result;
}

void Fill3DHoneycomb::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    // no rotation is supported for this infill pattern
    BoundingBox bb = expolygon.contour.bounding_box();
    coord_t     distance = coord_t(scale_(this->spacing) / params.density);

    // align bounding box to a multiple of our honeycomb grid module
    // (a module is 2*$distance since one $distance half-module is 
    // growing while the other $distance half-module is shrinking)
    bb.merge(align_to_grid(bb.min, Point(2*distance, 2*distance)));
    
    // generate pattern
    Polylines   polylines = makeGrid(
        scale_(this->z),
        distance,
        ceil(bb.size()(0) / distance) + 1,
        ceil(bb.size()(1) / distance) + 1,
        ((this->layer_id/thickness_layers) % 2) + 1);
    
    // move pattern in place
	for (Polyline &pl : polylines)
		pl.translate(bb.min);

    // clip pattern to boundaries, chain the clipped polylines
    polylines = intersection_pl(polylines, expolygon);

    // connect lines if needed
    if (params.dont_connect() || polylines.size() <= 1)
        append(polylines_out, chain_polylines(std::move(polylines)));
    else
        this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);
}

} // namespace Slic3r
