#ifndef SLA_HOLLOWING_HPP
#define SLA_HOLLOWING_HPP

#include <memory>
#include <libslic3r/SLA/Common.hpp>
#include <libslic3r/SLA/JobController.hpp>

namespace Slic3r {

class TriangleMesh;

namespace sla {

struct HollowingConfig
{
    double min_thickness    = 2.;
    double quality          = 0.5;
    double closing_distance = 0.5;
};

struct DrainHole
{
    Vec3f pos;
    Vec3f normal;
    float radius;
    float height;

    DrainHole()
        : pos(Vec3f::Zero()), normal(Vec3f::UnitZ()), radius(5.f), height(10.f)
    {}

    DrainHole(Vec3f p, Vec3f n, float r, float h)
        : pos(p), normal(n), radius(r), height(h)
    {}
    
    bool operator==(const DrainHole &sp) const;
    
    bool operator!=(const DrainHole &sp) const { return !(sp == (*this)); }
    
    template<class Archive> inline void serialize(Archive &ar)
    {
        ar(pos, normal, radius, height);
    }
};

using DrainHoles = std::vector<DrainHole>;

std::unique_ptr<TriangleMesh> generate_interior(const TriangleMesh &mesh,
                                                const HollowingConfig &  = {},
                                                const JobController &ctl = {});

}
}

#endif // HOLLOWINGFILTER_H
