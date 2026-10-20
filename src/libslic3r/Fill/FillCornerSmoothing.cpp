#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>
#include <utility>
#include <cmath>
#include <cstdlib>

#include "FillCornerSmoothing.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

// Gyroid flattens its waves to a chord times sagitta of (0.2 mm)^2, and their radius grows with the distance between
// the infill lines, which puts a vertex about every cbrt(0.068 mm^2 * distance) along its path: 0.85 mm at 5% and
// 0.4 mm at 50% density. It prints 0.91 of the path its density makes per area. A rounded pattern may take 80% of the
// vertices per area Gyroid takes, the rest being left for the travel moves a pattern of more pieces prints.
double smoothed_infill_vertex_density(const double spacing, const double density, const double path_per_area)
{
    if (spacing <= 0. || density <= 0. || path_per_area <= 0.)
        return 0.;
    const double distance        = spacing / density;
    const double gyroid_per_area = 0.91 / distance / std::cbrt(0.068 * distance);
    return 0.8 * gyroid_per_area / path_per_area;
}

// Turns sharper than this are left untouched: both ends of the curve replacing such a corner nearly
// coincide, so the corner would be rounded into a degenerate loop instead of a hairpin.
static constexpr const double min_smoothed_turn_cosine = -0.9;

// Spacing of the control points of the Bezier corner curves along each leg, as a fraction of the reach.
static constexpr const double quintic_spacing = 0.3;
static constexpr const double septic_spacing  = 0.2;

// The control points of the corner curve in a frame where the incoming leg runs along +x into the corner at
// the origin and the outgoing leg leaves it turning left. At each end, the first three control points are
// collinear and equally spaced: the tangent follows the adjoining leg and the second derivative is zero,
// so the curvature starts from zero, a G2 join. The septic curve keeps a fourth control point on each leg,
// which also zeroes the rate of the curvature there, a G3 join.
static std::vector<Vec2d> bezier_control_points(const InfillSmoothCurve curve, const double reach, const Vec2d &outgoing)
{
    const bool   septic  = curve == InfillSmoothCurve::Septic;
    const size_t per_leg = septic ? 4 : 3;
    const double spacing = (septic ? septic_spacing : quintic_spacing) * reach;
    std::vector<Vec2d> control;
    control.reserve(2 * per_leg);
    for (size_t i = 0; i < per_leg; ++i)
        control.emplace_back(-(reach - double(i) * spacing), 0.);
    for (size_t i = per_leg; i-- > 0;)
        control.emplace_back((reach - double(i) * spacing) * outgoing);
    return control;
}

// The first and second derivatives of a Bezier curve, by de Casteljau down to its last three points.
static std::pair<Vec2d, Vec2d> bezier_derivatives(const std::vector<Vec2d> &control, const double t)
{
    std::vector<Vec2d> work = control;
    for (size_t count = work.size(); count > 3; --count)
        for (size_t i = 0; i + 1 < count; ++i)
            work[i] = (1. - t) * work[i] + t * work[i + 1];
    const double degree = double(control.size() - 1);
    return { degree * ((1. - t) * (work[1] - work[0]) + t * (work[2] - work[1])),
             degree * (degree - 1.) * (work[2] - 2. * work[1] + work[0]) };
}

static Vec2d bezier_point(const std::vector<Vec2d> &control, const double t)
{
    std::vector<Vec2d> work = control;
    for (size_t count = work.size(); count > 1; --count)
        for (size_t i = 0; i + 1 < count; ++i)
            work[i] = (1. - t) * work[i] + t * work[i + 1];
    return work.front();
}

// The step of direction between the vertices of a corner whose curve peaks at the given curvature. A planner
// limits a junction turning by the step to v^2 = deviation * acceleration * cos(step / 2) / (1 - cos(step / 2)),
// and the curve allows v^2 = acceleration / curvature: the step is as coarse as reaches the speed of the curve.
// It is never finer than lets every vertex stand out of the chord of its neighbours by the tolerance, as
// simplifying the path to the tolerance would drop such a vertex again. Without a junction deviation, the
// polygon keeps within the tolerance of the curve.
static double vertex_step(const double curvature, const CornerSmoothing &smoothing)
{
    const double tolerance = smoothing.tolerance > 0. && std::isfinite(smoothing.tolerance) ? smoothing.tolerance : EPSILON;
    const double kept      = std::sqrt(2. * curvature * tolerance);
    if (smoothing.junction_deviation > 0.) {
        const double ratio = 1. / (curvature * smoothing.junction_deviation);
        return std::max(kept, 2. * std::acos(ratio / (1. + ratio)));
    }
    return std::sqrt(8. * curvature * tolerance);
}

double CornerSmoother::peak_curvature_by_reach(const double cosine)
{
    if (cosine == m_cached_curvature_cosine)
        return m_cached_curvature;

    const double turn = std::acos(std::clamp(cosine, -1., 1.));
    double       peak = 0.;
    if (m_smoothing.curve == InfillSmoothCurve::Arc) {
        peak = std::tan(0.5 * turn);
    } else {
        const std::vector<Vec2d> control = bezier_control_points(m_smoothing.curve, 1., Vec2d(cosine, std::sin(turn)));
        static constexpr const size_t samples = 256;
        for (size_t i = 0; i <= samples; ++i) {
            const auto [first, second] = bezier_derivatives(control, double(i) / double(samples));
            const double speed = first.norm();
            if (speed > EPSILON)
                peak = std::max(peak, std::abs(cross2(first, second)) / (speed * speed * speed));
        }
    }
    m_cached_curvature_cosine = cosine;
    m_cached_curvature        = peak;
    return peak;
}

const std::vector<Vec2d> &CornerSmoother::corner_vertices(const double reach, const double cosine, const size_t count)
{
    // Corners of the same size, turn and vertex count are congruent. An infill path walks over the very same
    // corner over and over again, the Hilbert curve over a single one.
    if (count == m_cached_count && reach == m_cached_reach && cosine == m_cached_cosine)
        return m_cached_vertices;

    const double turn     = std::acos(std::clamp(cosine, -1., 1.));
    const double sine     = std::sin(turn);
    const Vec2d  outgoing(cosine, sine);

    // The lines tangent to the curve at equal steps of direction, both legs included. Each vertex of the
    // rounded corner is where two consecutive ones cross, so each turns by the same step.
    std::vector<std::pair<Vec2d, Vec2d>> tangents;
    tangents.reserve(count + 1);
    tangents.emplace_back(Vec2d(-reach, 0.), Vec2d(1., 0.));
    if (m_smoothing.curve == InfillSmoothCurve::Arc) {
        const double radius = reach / std::tan(0.5 * turn);
        const Vec2d  center(-reach, radius);
        for (size_t i = 1; i < count; ++i) {
            const double angle = turn * double(i) / double(count);
            tangents.emplace_back(center + radius * Vec2d(std::sin(angle), -std::cos(angle)), Vec2d(std::cos(angle), std::sin(angle)));
        }
    } else {
        const std::vector<Vec2d> control = bezier_control_points(m_smoothing.curve, reach, outgoing);
        double                   from    = 0.;
        for (size_t i = 1; i < count; ++i) {
            // The direction of the tangent grows monotonically along the curve.
            const double angle = turn * double(i) / double(count);
            double       low = from, high = 1.;
            for (int iteration = 0; iteration < 48; ++iteration) {
                const double middle  = 0.5 * (low + high);
                const Vec2d  tangent = bezier_derivatives(control, middle).first;
                if (std::atan2(tangent.y(), tangent.x()) < angle)
                    low = middle;
                else
                    high = middle;
            }
            from = low;
            const double t = 0.5 * (low + high);
            tangents.emplace_back(bezier_point(control, t), bezier_derivatives(control, t).first.normalized());
        }
    }
    tangents.emplace_back(reach * outgoing, outgoing);

    m_cached_vertices.clear();
    m_cached_vertices.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto &[point, direction]           = tangents[i];
        const auto &[next_point, next_direction] = tangents[i + 1];
        const Vec2d vertex = point + (cross2(next_point - point, next_direction) / cross2(direction, next_direction)) * direction;
        // In the (incoming, outgoing) basis, which is not orthonormal for turns other than a right angle.
        const double along_outgoing = vertex.y() / sine;
        m_cached_vertices.emplace_back(vertex.x() - along_outgoing * cosine, along_outgoing);
    }
    m_cached_reach  = reach;
    m_cached_cosine = cosine;
    m_cached_count  = count;
    return m_cached_vertices;
}

bool CornerSmoother::is_on_straight_run(const Vec2d &previous, const Vec2d &vertex, const Vec2d &next)
{
    const Vec2d  incoming_leg    = vertex - previous;
    const Vec2d  outgoing_leg    = next - vertex;
    const double incoming_length = incoming_leg.norm();
    const double outgoing_length = outgoing_leg.norm();
    // A vertex repeating one of its neighbours carries no direction of its own.
    if (incoming_length < EPSILON || outgoing_length < EPSILON)
        return true;

    const Vec2d incoming = incoming_leg / incoming_length;
    const Vec2d outgoing = outgoing_leg / outgoing_length;
    return incoming.dot(outgoing) > 0. &&
           std::abs(incoming.x() * outgoing.y() - incoming.y() * outgoing.x()) < EPSILON;
}

void CornerSmoother::round_corner(const Vec2d &previous, const Vec2d &corner, const Vec2d &next)
{
    m_corner_points.clear();

    const Vec2d  incoming_leg = corner - previous;
    const Vec2d  outgoing_leg = next - corner;
    const double incoming_length = incoming_leg.norm();
    const double outgoing_length = outgoing_leg.norm();
    if (incoming_length < EPSILON || outgoing_length < EPSILON) {
        m_corner_points.emplace_back(corner);
        return;
    }

    const Vec2d  incoming = incoming_leg / incoming_length;
    const Vec2d  outgoing = outgoing_leg / outgoing_length;
    const double cross    = incoming.x() * outgoing.y() - incoming.y() * outgoing.x();
    const double cosine   = incoming.dot(outgoing);
    // A collinear vertex is no corner at all, and a hairpin cannot be rounded, see above.
    if (std::abs(cross) < EPSILON || cosine < min_smoothed_turn_cosine) {
        m_corner_points.emplace_back(corner);
        return;
    }

    // Consuming at most half of the shorter leg keeps the curves of two adjacent corners apart.
    double reach = m_corner_distance_ratio * std::min(incoming_length, outgoing_length);
    if (m_max_corner_distance > 0.)
        reach = std::min(reach, m_max_corner_distance);

    const double turn  = std::acos(std::clamp(cosine, -1., 1.));
    const double shape = peak_curvature_by_reach(cosine);
    size_t       count = size_t(std::ceil(turn / vertex_step(shape / reach, m_smoothing) - EPSILON));
    // The vertices a rounded corner adds to its own, out of what the half of each leg it owns allows. What a corner
    // leaves of it goes on to the next ones, so that corners whose own share falls short of a curve take turns.
    const bool   budgeted = m_smoothing.max_vertex_density > 0.;
    const double extra    = budgeted ? m_extra_vertices + m_smoothing.max_vertex_density * 0.5 * (incoming_length + outgoing_length) - 1. : 0.;
    if (budgeted && extra + 1. < double(count)) {
        count = size_t(std::max(0., std::floor(extra + 1.)));
        // Too few vertices for the whole curve: tighten it to the radius at which they are as fast as the curve,
        // so the corner is still quicker than a sharp one instead of a long slow polygon.
        if (count > 1 && m_smoothing.junction_deviation > 0.) {
            const double step   = turn / double(count);
            const double radius = std::max(8. * m_smoothing.junction_deviation, 2. * m_smoothing.tolerance) / (step * step);
            reach               = std::min(reach, radius * shape);
        }
    }
    const bool rounded = count > 1 && (!m_corner_filter || m_corner_filter(corner - reach * incoming, corner + reach * outgoing));
    if (budgeted)
        m_extra_vertices = std::max(0., extra - (rounded ? double(count - 1) : 0.));
    if (!rounded) {
        m_corner_points.emplace_back(corner);
        return;
    }

    const std::vector<Vec2d> &vertices = corner_vertices(reach, cosine, count);
    m_corner_points.reserve(vertices.size());
    for (const Vec2d &vertex : vertices)
        m_corner_points.emplace_back(corner + vertex.x() * incoming + vertex.y() * outgoing);
}

// Rounds the corners of a scaled point sequence. A polygon closes implicitly, so all of its vertices
// are corners; a polyline is an open path that keeps both of its ends, even where they coincide - a
// path returning to where it started retraces its way back and is not a loop.
static Points smooth_corners(const Points &points, const bool polygon, CornerSmoother &smoother)
{
    // A polygon has no free ends, so its first vertex is a corner like any other. Rounding it takes
    // feeding the smoother the last vertex first, whose own output point is then dropped again.
    size_t skip = polygon ? 1 : 0;

    Points smoothed;
    smoothed.reserve(2 * points.size());
    auto emit = [&smoothed, &skip](const Vec2d &point) {
        if (skip > 0) {
            --skip;
            return;
        }
        smoothed.emplace_back(coord_t(std::floor(point.x() + 0.5)), coord_t(std::floor(point.y() + 0.5)));
    };

    if (polygon)
        smoother.push(points.back().cast<double>(), emit);
    for (const Point &point : points)
        smoother.push(point.cast<double>(), emit);
    if (polygon)
        // Wrap the first vertex around, so that the last one is a corner as well.
        smoother.push(points.front().cast<double>(), emit);
    smoother.flush(emit);

    if (polygon)
        // The flushed point is the wrapped first vertex, which a polygon does not store.
        smoothed.pop_back();
    return smoothed;
}

void smooth_polyline_corners(Polyline &polyline, const CornerSmoothing &smoothing, const double max_corner_distance,
                             const CornerFilter &corner_filter)
{
    CornerSmoother smoother(smoothing, max_corner_distance, corner_filter);
    if (!smoother.enabled() || polyline.size() < 3)
        return;

    polyline.points = smooth_corners(polyline.points, false, smoother);
    // Rounding back to the integer grid may collapse neighbouring vertices of a corner.
    polyline.remove_duplicate_points();
}

void smooth_polylines_corners(Polylines &polylines, const CornerSmoothing &smoothing, const double max_corner_distance,
                              const CornerFilter &corner_filter)
{
    if (sanitize_smooth_factor(smoothing.factor) == 0.)
        return;
    for (Polyline &polyline : polylines)
        smooth_polyline_corners(polyline, smoothing, max_corner_distance, corner_filter);
}

void smooth_polygons_corners(Polygons &polygons, const CornerSmoothing &smoothing, const double max_corner_distance,
                             const CornerFilter &corner_filter)
{
    CornerSmoother smoother(smoothing, max_corner_distance, corner_filter);
    if (!smoother.enabled())
        return;

    for (Polygon &polygon : polygons) {
        if (polygon.size() < 3)
            continue;
        polygon.points = smooth_corners(polygon.points, true, smoother);
        polygon.remove_duplicate_points();
        // The vertices of the first and of the last corner may meet on the leg they share. A polygon closes
        // implicitly, so it must not repeat its first vertex at the end.
        if (polygon.points.size() > 1 && polygon.points.front() == polygon.points.back())
            polygon.points.pop_back();
    }
}

} // namespace Slic3r
