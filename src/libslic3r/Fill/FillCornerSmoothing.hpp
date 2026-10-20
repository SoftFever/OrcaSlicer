#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "../Point.hpp"
#include "../Polygon.hpp"
#include "../Polyline.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r {

// Orca: NaN or infinite factors disable the smoothing, everything else is clamped to <0, 1>.
inline double sanitize_smooth_factor(double smooth_factor)
{
    return std::isfinite(smooth_factor) ? std::clamp(smooth_factor, 0., 1.) : 0.;
}

// Orca: The most vertices per mm of path the rounded corners of a sparse infill of the given line spacing (mm) and
// density (fraction) may take, for a pattern printing the given length of path per area (1/mm). It bounds the G-code
// rounding adds where corners are dense.
double smoothed_infill_vertex_density(double spacing, double density, double path_per_area);

// Orca: How the corners of an infill path are rounded. Lengths are in the units of the points the smoother
// is fed, densities per one of those units.
struct CornerSmoothing
{
    // Fraction of the shorter adjoining leg a curve may reach along it, relative to half of that leg.
    double            factor { 0. };
    InfillSmoothCurve curve { InfillSmoothCurve::Quintic };
    // The G-code resolution: a rounded corner deviates from its curve by up to it, and its vertices stand out
    // of the path by at least it, so that simplifying the path to it keeps them.
    double            tolerance { 0. };
    // The junction deviation the machine plans with, or 0 if it does not plan by one. A corner is split
    // into vertices that turn little enough for its junctions to be as fast as the curve allows, and no finer.
    double            junction_deviation { 0. };
    // The most vertices a corner may become per unit of the path around it, 0 for no limit. It bounds the
    // G-code a rounded path takes.
    double            max_vertex_density { 0. };

    // The same smoothing for points scaled by the given factor.
    CornerSmoothing scaled(double scale) const
    {
        CornerSmoothing out = *this;
        out.tolerance *= scale;
        out.junction_deviation *= scale;
        out.max_vertex_density /= scale;
        return out;
    }
};

// Decides whether a corner may be replaced by the curve that leaves the path at `from` and rejoins it
// at `to`, both in the coordinate system of the pushed points. Rounding cuts toward the inside of the
// turn, so a path that is not clipped to the fill region afterwards needs this to stay inside it.
using CornerFilter = std::function<bool(const Vec2d &from, const Vec2d &to)>;

// Orca: Replaces the sharp vertices of an infill path with curves joining the adjoining straight legs, so the
// toolhead does not have to slow down to a stop in every corner. A curve is printed as the polygon of its
// tangents taken at equal steps of direction, so every vertex of a rounded corner turns by the same angle:
// a planner limits the speed at a vertex by how much it turns, and equal turns make the most of each vertex.
// Points are pushed one by one, because the plane path fills produce their path on the fly, and every point
// of the smoothed path is handed over to the caller supplied emit callback. Points that collapse onto each
// other once rounded to the integer grid of the caller are left to the caller to drop.
class CornerSmoother
{
public:
    // max_corner_distance caps how far a curve may reach along a leg, in the units of the pushed points; it
    // bounds how far a rounded corner moves away from the original path, which matters where the legs are
    // much longer than the spacing of the pattern. Zero leaves the reach uncapped.
    CornerSmoother(const CornerSmoothing &smoothing, double max_corner_distance = 0., CornerFilter corner_filter = {})
        : m_smoothing(smoothing)
        , m_corner_distance_ratio(0.5 * sanitize_smooth_factor(smoothing.factor))
        , m_max_corner_distance(max_corner_distance)
        , m_corner_filter(std::move(corner_filter))
    {}

    bool enabled() const { return m_corner_distance_ratio > 0.; }

    template<typename Emit> void push(const Vec2d &point, Emit &emit)
    {
        if (m_held == 0) {
            // The first point of a path is an end, not a corner, and stays where it is.
            emit(point);
            m_window[m_held++] = point;
            return;
        }
        if (m_held > 1 && is_on_straight_run(m_window[m_held - 2], m_window[m_held - 1], point)) {
            // The newest vertex only splits a straight leg, so the leg runs on to this point instead.
            m_window[m_held - 1] = point;
            return;
        }
        if (m_held < 3) {
            m_window[m_held++] = point;
            return;
        }
        // Both legs of the middle vertex are complete now, so its curve can no longer grow.
        emit_corner(m_window[0], m_window[1], m_window[2], emit);
        m_window[0] = m_window[1];
        m_window[1] = m_window[2];
        m_window[2] = point;
    }

    // Emits the last point of the path and prepares the smoother for a new one.
    template<typename Emit> void flush(Emit &emit)
    {
        if (m_held > 2)
            emit_corner(m_window[0], m_window[1], m_window[2], emit);
        if (m_held > 1)
            emit(m_window[m_held - 1]);
        m_held           = 0;
        m_extra_vertices = 0.;
    }

private:
    template<typename Emit> void emit_corner(const Vec2d &previous, const Vec2d &corner, const Vec2d &next, Emit &emit)
    {
        round_corner(previous, corner, next);
        for (const Vec2d &corner_point : m_corner_points)
            emit(corner_point);
    }

    // Tells a vertex that only continues a straight leg (or repeats its predecessor) from a corner.
    // A path doubling back on itself is not one, that vertex is a hairpin and stays where it is.
    static bool is_on_straight_run(const Vec2d &previous, const Vec2d &vertex, const Vec2d &next);
    // Fills m_corner_points with the points replacing the corner vertex.
    void round_corner(const Vec2d &previous, const Vec2d &corner, const Vec2d &next);
    // The peak curvature of the corner curve times its reach, which depends on the turn only.
    double peak_curvature_by_reach(double cosine);
    // The vertices of the rounded corner in the (incoming, outgoing) basis of the corner, relative to it.
    // Cached, as an infill path repeats the same corner over and over again.
    const std::vector<Vec2d> &corner_vertices(double reach, double cosine, size_t count);

    const CornerSmoothing m_smoothing;
    // Fraction of the shorter adjoining leg consumed on each side of a corner. Half of a leg is the
    // maximum, otherwise the curves of two adjacent corners would overlap.
    const double       m_corner_distance_ratio;
    const double       m_max_corner_distance;
    const CornerFilter m_corner_filter;
    std::vector<Vec2d> m_corner_points;
    // The vertices the corners rounded so far left of the budget of the path.
    double             m_extra_vertices { 0. };

    std::vector<Vec2d> m_cached_vertices;
    double             m_cached_reach { 0. };
    double             m_cached_cosine { 2. };
    size_t             m_cached_count { 0 };
    double             m_cached_curvature { 0. };
    double             m_cached_curvature_cosine { 2. };

    // The corners seen last, kept free of vertices that merely split a straight leg. The middle one
    // is rounded once the third arrives, which is what makes its outgoing leg final.
    std::array<Vec2d, 3> m_window { Vec2d::Zero(), Vec2d::Zero(), Vec2d::Zero() };
    // How many of them are filled in.
    int                  m_held { 0 };
};

// Rounds the corners of already scaled paths in place. Paths of less than three points are left alone.
// Both ends of a polyline are kept where they are, even when they coincide: such a path retraces its
// way back and joining its ends would turn it into a loop. See CornerSmoother for max_corner_distance.
void smooth_polyline_corners(Polyline &polyline, const CornerSmoothing &smoothing, double max_corner_distance = 0.,
                             const CornerFilter &corner_filter = {});
void smooth_polylines_corners(Polylines &polylines, const CornerSmoothing &smoothing, double max_corner_distance = 0.,
                              const CornerFilter &corner_filter = {});
// Polygons close implicitly, so every one of their vertices is a corner.
void smooth_polygons_corners(Polygons &polygons, const CornerSmoothing &smoothing, double max_corner_distance = 0.,
                             const CornerFilter &corner_filter = {});

// The G2 curve rounded to the given chordal tolerance, with no machine or density limit.
inline void smooth_polyline_corners(Polyline &polyline, double smooth_factor, double tolerance)
{
    smooth_polyline_corners(polyline, CornerSmoothing{smooth_factor, InfillSmoothCurve::Quintic, tolerance});
}
inline void smooth_polygons_corners(Polygons &polygons, double smooth_factor, double tolerance)
{
    smooth_polygons_corners(polygons, CornerSmoothing{smooth_factor, InfillSmoothCurve::Quintic, tolerance});
}

} // namespace Slic3r
