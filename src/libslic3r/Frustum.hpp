#ifndef slic3r_Frustum_hpp_
#define slic3r_Frustum_hpp_

#include "Point.hpp"
#include "BoundingBox.hpp"
namespace Slic3r {
class Frustum
{
public:
    Frustum()=default;
    ~Frustum() = default;

    class Plane {
    public:
        enum PlaneIntersects { Intersects_Cross = 0, Intersects_Tangent = 1, Intersects_Front = 2, Intersects_Back = 3 };
        void set(const Vec3f &n, const Vec3f &pt)
        {
            normal_ = n.normalized();
            center_ = pt;
            d_      = -normal_.dot(pt);
        }

        float distance(const Vec3f &pt) const { return normal_.dot(pt) + d_; }

        inline const Vec3f &getNormal() const { return normal_; }
        const Vec3f &       getCenter() const { return center_; }
        Plane::PlaneIntersects intersects(const BoundingBoxf3 &box) const;
        //// check intersect with point (world space)
        Plane::PlaneIntersects intersects(const Vec3f &p0) const;
        // check intersect with line segment (world space)
        Plane::PlaneIntersects intersects(const Vec3f &p0, const Vec3f &p1) const;
        // check intersect with triangle (world space)
        Plane::PlaneIntersects intersects(const Vec3f &p0, const Vec3f &p1, const Vec3f &p2) const;
    private:
        Vec3f normal_;
        Vec3f center_;
        float d_ = 0;
    };

    bool intersects(const BoundingBoxf3 &box, bool is_perspective) const;
    // check intersect with point (world space)
    bool intersects(const Vec3f &p0) const;
    // check intersect with line segment (world space)
    bool intersects(const Vec3f &p0, const Vec3f &p1) const;
    // check intersect with triangle (world space)
    bool intersects(const Vec3f &p0, const Vec3f &p1, const Vec3f &p2) const;

    Plane planes[6];
    /* corners[0]: nearTopLeft;
     * corners[1]: nearTopRight;
     * corners[2]: nearBottomLeft;
     * corners[3]: nearBottomRight;
     * corners[4]: farTopLeft;
     * corners[5]: farTopRight;
     * corners[6]: farBottomLeft;
     * corners[7]: farBottomRight;
     */
    Vec3f corners[8];

    BoundingBoxf3 bbox;
};

enum FrustumClipMask {
    POSITIVE_X = 1 << 0,
    NEGATIVE_X = 1 << 1,
    POSITIVE_Y = 1 << 2,
    NEGATIVE_Y = 1 << 3,
    POSITIVE_Z = 1 << 4,
    NEGATIVE_Z = 1 << 5,
};

const int FrustumClipMaskArray[6] = {
    FrustumClipMask::POSITIVE_X, FrustumClipMask::NEGATIVE_X, FrustumClipMask::POSITIVE_Y, FrustumClipMask::NEGATIVE_Y, FrustumClipMask::POSITIVE_Z, FrustumClipMask::NEGATIVE_Z,
};

const Vec4f FrustumClipPlane[6] = {{-1, 0, 0, 1}, {1, 0, 0, 1}, {0, -1, 0, 1}, {0, 1, 0, 1}, {0, 0, -1, 1}, {0, 0, 1, 1}};
}

#endif
