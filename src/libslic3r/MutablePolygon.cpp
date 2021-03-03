#include "MutablePolygon.hpp"
#include "Line.hpp"

namespace Slic3r {

// Remove exact duplicate points. May reduce the polygon down to empty polygon.
void remove_duplicates(MutablePolygon &polygon)
{
    if (! polygon.empty()) {
        auto begin = polygon.begin();
        auto it    = begin;
        for (++ it; it != begin;) {
            auto prev = it.prev();
            if (*prev == *it)
                it = it.remove();
            else
                ++ it;
        }
    }
}

// Remove nearly duplicate points. May reduce the polygon down to empty polygon.
void remove_duplicates(MutablePolygon &polygon, double eps)
{
    if (! polygon.empty()) {
        auto eps2 = eps * eps;
        auto begin = polygon.begin();
        auto it = begin;
        for (++ it; it != begin;) {
            auto prev = it.prev();
            if ((*it - *prev).cast<double>().squaredNorm() < eps2)
                it = it.remove();
            else
                ++ it;
        }
    }
}

// Sample a point on line (a, b) at distance "dist" from ref_pt.
// If two points fulfill the condition, then the first one (closer to point a) is taken.
// If none of the two points falls on line (a, b), return false.
template<typename VectorType>
static inline VectorType point_on_line_at_dist(const VectorType &a, const VectorType &b, const VectorType &ref_pt, const double dist)
{
    using T = typename VectorType::Scalar;
    auto   v   = b - a;
    auto   l2  = v.squaredNorm();
    assert(l2 > T(0));
    auto   vpt = ref_pt - a;
    // Parameter of the foot point of ref_pt on line (a, b).
    auto   t   = v.dot(vpt) / l2;
    // Foot point of ref_pt on line (a, b).
    auto   foot_pt = a + t * v;
    auto   dfoot2 = vpt.squaredNorm() - (foot_pt - ref_pt).squaredNorm();
    // Distance of the result point from the foot point, normalized to length of (a, b).
    auto   dfoot  = dfoot2 > T(0) ? sqrt(dfoot2) / sqrt(l2) : T(0);
    auto   t_result = t - dfoot;
    if (t_result < T(0))
        t_result = t + dfoot;
    t_result = Slic3r::clamp(0., 1., t_result);
    return a + v * t;
}

static bool smooth_corner_complex(const Vec2d p1, MutablePolygon::iterator &it0, MutablePolygon::iterator &it2, const double shortcut_length)
{
    // walk away from the corner until the shortcut > shortcut_length or it would smooth a piece inward
    // - walk in both directions untill shortcut > shortcut_length
    // - stop walking in one direction if it would otherwise cut off a corner in that direction
    // - same in the other direction
    // - stop if both are cut off
    // walk by updating p0_it and p2_it
    double shortcut_length2    = shortcut_length * shortcut_length;
    bool   forward_is_blocked  = false;
    bool   forward_is_too_far  = false;
    bool   backward_is_blocked = false;
    bool   backward_is_too_far = false;
    for (;;) {
        const bool forward_has_converged  = forward_is_blocked  || forward_is_too_far;
        const bool backward_has_converged = backward_is_blocked || backward_is_too_far;
        if (forward_has_converged && backward_has_converged) {
            if (forward_is_too_far && backward_is_too_far && (*it0.prev() - *it2.next()).cast<double>().squaredNorm() < shortcut_length2) {
                // Trim the narrowing region.
                -- it0;
                ++ it2;
                forward_is_too_far  = false;
                backward_is_too_far = false;
                continue;
            } else
                break;
        }

        const Vec2d p0 = it0->cast<double>();
        const Vec2d p2 = it2->cast<double>();
        if (! forward_has_converged && (backward_has_converged || (p2 - p1).squaredNorm() < (p0 - p1).squaredNorm())) {
            // walk forward
            const auto  it2_2 = it2.next();
            const Vec2d p2_2  = it2_2->cast<double>();
            if (cross2(p2 - p0, p2_2 - p0) > 0) {
                forward_is_blocked  = true;
            } else if ((p2_2 - p0).squaredNorm() > shortcut_length2) {
                forward_is_too_far  = true;
            } else {
                it2                 = it2_2; // make one step in the forward direction
                backward_is_blocked = false; // invalidate data about backward walking
                backward_is_too_far = false;
            }
        } else {
            // walk backward
            const auto  it0_2 = it0.prev();
            const Vec2d p0_2  = it0_2->cast<double>();
            if (cross2(p0_2 - p0, p2 - p0_2) > 0) {
                backward_is_blocked = true;
            } else if ((p2 - p0_2).squaredNorm() > shortcut_length2) {
                backward_is_too_far = true;
            } else {
                it0                = it0_2; // make one step in the backward direction
                forward_is_blocked = false; // invalidate data about forward walking
                forward_is_too_far = false;
            }
        }

        if (it0.prev() == it2 || it0 == it2) {
            // stop if we went all the way around the polygon
            // this should only be the case for hole polygons (?)
            if (forward_is_too_far && backward_is_too_far) {
                // in case p0_it.prev() == p2_it :
                //     /                                                .
                //    /                /|
                //   |       becomes  | |
                //    \                \|
                //     \                                                .
                // in case p0_it == p2_it :
                //     /                                                .
                //    /    becomes     /|
                //    \                \|
                //     \                                                .
                break;
            } else {
                // this whole polygon can be removed
                return true;
            }
        }
    }

    const Vec2d   p0     = it0->cast<double>();
    const Vec2d   p2     = it2->cast<double>();
    const Vec2d   v02    = p2 - p0;
    const int64_t l2_v02 = v02.squaredNorm();
    if (std::abs(l2_v02 - shortcut_length2) < shortcut_length * 10) // i.e. if (size2 < l * (l+10) && size2 > l * (l-10))
    { // v02 is approximately shortcut length
        // handle this separately to avoid rounding problems below in the getPointOnLineWithDist function
        // p0_it and p2_it are already correct
    } else if (! backward_is_blocked && ! forward_is_blocked) {
        const auto  l_v02 = sqrt(l2_v02);
        const Vec2d p0_2  = it0.prev()->cast<double>();
        const Vec2d p2_2  = it2.next()->cast<double>();
        double t = Slic3r::clamp(0., 1., (shortcut_length - l_v02) / ((p2_2 - p0_2).norm() - l_v02));
        it0 = it0.prev().insert((p0 + (p0_2 - p0) * t).cast<coord_t>());
        it2 = it2.insert((p2 + (p2_2 - p2) * t).cast<coord_t>());
    } else if (! backward_is_blocked) {
        it0 = it0.prev().insert(point_on_line_at_dist(p0, Vec2d(it0.prev()->cast<double>()), p2, shortcut_length).cast<coord_t>());
    } else if (! forward_is_blocked) {
        it2 = it2.insert(point_on_line_at_dist(p2, Vec2d(it2.next()->cast<double>()), p0, shortcut_length).cast<coord_t>());
    } else {
        //        |
        //      __|2
        //     | /  > shortcut cannot be of the desired length
        //  ___|/                                                       .
        //     0
        // both are blocked and p0_it and p2_it are already correct
    }
    // Delete all the points between it0 and it2.
    while (it0.next() != it2)
        it0.next().remove();
    return false;
}

void smooth_outward(MutablePolygon &polygon, double shortcut_length)
{
    remove_duplicates(polygon, scaled<double>(0.01));

    const int                     shortcut_length2 = shortcut_length * shortcut_length;
    static constexpr const double cos_min_angle    = -0.70710678118654752440084436210485; // cos(135 degrees)

    MutablePolygon::iterator it1 = polygon.begin();
    do {
        const Vec2d p1  = it1->cast<double>();
        auto        it0 = it1.prev();
        auto        it2 = it1.next();
        const Vec2d p0  = it0->cast<double>();
        const Vec2d p2  = it2->cast<double>();
        const Vec2d v1  = p0 - p1;
        const Vec2d v2  = p2 - p1;
        const double cos_angle = v1.dot(v2);
        if (cos_angle < cos_min_angle && cross2(v1, v2) < 0) {
            // Simplify the sharp angle.
            const Vec2d  v02   = p2 - p0;
            const double l2_v02 = v02.squaredNorm();
            if (l2_v02 >= shortcut_length2) {
                // Trim an obtuse corner.
                it1.remove();
                if (l2_v02 > Slic3r::sqr(shortcut_length + SCALED_EPSILON)) {
                    double l2_1 = v1.squaredNorm();
                    double l2_2 = v2.squaredNorm();
                    bool trim = true;
                    if (cos_angle > 0.9999) {
                        // The triangle p0, p1, p2 is likely degenerate.
                        // Measure height of the triangle.
                        double d2 = l2_1 > l2_2 ? line_alg::distance_to_squared(Linef{ p0, p1 }, p2) : line_alg::distance_to_squared(Linef{ p2, p1 }, p0);
                        if (d2 < Slic3r::sqr(scaled<double>(0.02)))
                            trim = false;
                    }
                    if (trim) {
                        Vec2d  bisector  = v1 / l2_1 + v2 / l2_2;
                        double d1        = v1.dot(bisector) / l2_1;
                        double d2        = v2.dot(bisector) / l2_2;
                        double lbisector = bisector.norm();
                        if (d1 < shortcut_length && d2 < shortcut_length) {
                            it0.insert((p1 + v1 * (shortcut_length / d1)).cast<coord_t>())
                               .insert((p1 + v2 * (shortcut_length / d2)).cast<coord_t>());
                        } else if (v1.squaredNorm() < v2.squaredNorm())
                            it0.insert(point_on_line_at_dist(p1, p2, p0, shortcut_length).cast<coord_t>());
                        else
                            it0.insert(point_on_line_at_dist(p1, p0, p2, shortcut_length).cast<coord_t>());
                    }
                }
            } else {
                bool remove_poly = smooth_corner_complex(p1, it0, it2, shortcut_length); // edits p0_it and p2_it!
                if (remove_poly) {
                    // don't convert ListPolygon into result
                    return;
                }
            }
            // update:
            it1 = it2; // next point to consider for whether it's an internal corner
        }
        else
            ++ it1;
    } while (it1 != polygon.begin());
}

} // namespace Slic3r
