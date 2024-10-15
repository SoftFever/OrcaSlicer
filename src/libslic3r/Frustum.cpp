#include "Frustum.hpp"
#include <cmath>
namespace Slic3r {
Frustum::Plane::PlaneIntersects Frustum::Plane::intersects(const BoundingBoxf3 &box) const
{
    Vec3f center = ((box.min + box.max) * 0.5f).cast<float>();
    Vec3f extent = ((box.max - box.min) * 0.5f).cast<float>();
    float d      = distance(center);
    float r      = fabsf(extent.x() * normal_.x()) + fabsf(extent.y() * normal_.y()) + fabsf(extent.z() * normal_.z());
    if (d == r) {
        return Plane::Intersects_Tangent;
    } else if (std::abs(d) < r) {
        return Plane::Intersects_Cross;
    }
    return (d > 0.0f) ? Plane::Intersects_Front : Plane::Intersects_Back;
}
Frustum::Plane::PlaneIntersects Frustum::Plane::intersects(const Vec3f &p0) const
{
    float d = distance(p0);
    if (d == 0) {
        return Plane::Intersects_Tangent;
    }
    return (d > 0.0f) ? Plane::Intersects_Front : Plane::Intersects_Back;
}
Frustum::Plane::PlaneIntersects Frustum::Plane::intersects(const Vec3f &p0, const Vec3f &p1) const
{
    Plane::PlaneIntersects state0 = intersects(p0);
    Plane::PlaneIntersects state1 = intersects(p1);
    if (state0 == state1) {
        return state0;
    }
    if (state0 == Plane::Intersects_Tangent || state1 == Plane::Intersects_Tangent) {
        return Plane::Intersects_Tangent;
    }

    return Plane::Intersects_Cross;
}
Frustum::Plane::PlaneIntersects Frustum::Plane::intersects(const Vec3f &p0, const Vec3f &p1, const Vec3f &p2) const
{
    Plane::PlaneIntersects state0 = intersects(p0, p1);
    Plane::PlaneIntersects state1 = intersects(p0, p2);
    Plane::PlaneIntersects state2 = intersects(p1, p2);

    if (state0 == state1 && state0 == state2) {
        return state0; }

    if (state0 == Plane::Intersects_Cross || state1 == Plane::Intersects_Cross || state2 == Plane::Intersects_Cross) {
        return Plane::Intersects_Cross;
    }

    return Plane::Intersects_Tangent;
}

bool Frustum::intersects(const BoundingBoxf3 &box, bool is_perspective) const
{
    if (is_perspective) {
        for (auto &plane : planes) {
            if (plane.intersects(box) == Plane::Intersects_Back) {
                return false;
            }
        }
    }
    // check box intersects
    if (!bbox.intersects(box)) {
        return false;
    }
    return true;
}

bool Frustum::intersects(const Vec3f &p0) const {
    for (auto &plane : planes) {
        if (plane.intersects(p0) == Plane::Intersects_Back) { return false; }
    }
    return true;
}

bool Frustum::intersects(const Vec3f &p0, const Vec3f &p1) const
{
    for (auto &plane : planes) {
        if (plane.intersects(p0, p1) == Plane::Intersects_Back) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects(const Vec3f &p0, const Vec3f &p1, const Vec3f &p2) const
{
    for (auto &plane : planes) {
        if (plane.intersects(p0, p1, p2) == Plane::Intersects_Back) {
            return false;
        }
    }
    return true;
}

} // namespace Slic3r
