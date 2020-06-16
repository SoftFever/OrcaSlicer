#ifndef SUPPORTTREEMESHER_HPP
#define SUPPORTTREEMESHER_HPP

#include "libslic3r/Point.hpp"

#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/SLA/Contour3D.hpp"

namespace Slic3r { namespace sla {

using Portion = std::tuple<double, double>;

inline Portion make_portion(double a, double b)
{
    return std::make_tuple(a, b);
}

Contour3D sphere(double  rho,
                 Portion portion = make_portion(0., 2. * PI),
                 double  fa      = (2. * PI / 360.));

// Down facing cylinder in Z direction with arguments:
// r: radius
// h: Height
// ssteps: how many edges will create the base circle
// sp: starting point
Contour3D cylinder(double r, double h, size_t steps = 45, const Vec3d &sp = Vec3d::Zero());

Contour3D pinhead(double r_pin, double r_back, double length, size_t steps = 45);

Contour3D pedestal(const Vec3d &pt, double baseheight, double radius, size_t steps = 45);

inline Contour3D get_mesh(const Head &h, size_t steps)
{
    Contour3D mesh = pinhead(h.r_pin_mm, h.r_back_mm, h.width_mm, steps);

    // To simplify further processing, we translate the mesh so that the
    // last vertex of the pointing sphere (the pinpoint) will be at (0,0,0)
    for(auto& p : mesh.points) p.z() -= (h.fullwidth() - h.r_back_mm);

    using Quaternion = Eigen::Quaternion<double>;

    // We rotate the head to the specified direction The head's pointing
    // side is facing upwards so this means that it would hold a support
    // point with a normal pointing straight down. This is the reason of
    // the -1 z coordinate
    auto quatern = Quaternion::FromTwoVectors(Vec3d{0, 0, -1}, h.dir);

    for(auto& p : mesh.points) p = quatern * p + h.pos;

    return mesh;
}

inline Contour3D get_mesh(const Pillar &p, size_t steps)
{
    if(p.height > EPSILON) { // Endpoint is below the starting point
        // We just create a bridge geometry with the pillar parameters and
        // move the data.
        return cylinder(p.r, p.height, steps, p.endpoint());
    }

    return {};
}

inline Contour3D get_mesh(const Pedestal &p, size_t steps)
{
    return pedestal(p.pos, p.height, p.radius, steps);
}

inline Contour3D get_mesh(const Junction &j, size_t steps)
{
    Contour3D mesh = sphere(j.r, make_portion(0, PI), 2 *PI / steps);
    for(auto& p : mesh.points) p += j.pos;
    return mesh;
}

inline Contour3D get_mesh(const Bridge &br, size_t steps)
{
    using Quaternion = Eigen::Quaternion<double>;
    Vec3d v = (br.endp - br.startp);
    Vec3d dir = v.normalized();
    double d = v.norm();

    Contour3D mesh = cylinder(br.r, d, steps);

    auto quater = Quaternion::FromTwoVectors(Vec3d{0,0,1}, dir);
    for(auto& p : mesh.points) p = quater * p + br.startp;

    return mesh;
}

}}

#endif // SUPPORTTREEMESHER_HPP
