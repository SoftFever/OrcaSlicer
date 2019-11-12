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
    Vec3f m_pos;
    Vec3f m_normal;
    float m_radius;
    float m_height;
    
    DrainHole()
        : m_pos(Vec3f::Zero()), m_normal(Vec3f::UnitZ()), m_radius(5.f),
        m_height(10.f)
    {}
    
    DrainHole(Vec3f position, Vec3f normal, float radius, float height)
        : m_pos(position)
        , m_normal(normal)
        , m_radius(radius)
        , m_height(height)
    {}
    
    bool operator==(const DrainHole &sp) const;
    
    bool operator!=(const DrainHole &sp) const { return !(sp == (*this)); }
    
    template<class Archive> inline void serialize(Archive &ar)
    {
        ar(m_pos, m_normal, m_radius, m_height);
    }
};

std::unique_ptr<TriangleMesh> generate_interior(const TriangleMesh &mesh,
                                                const HollowingConfig &  = {},
                                                const JobController &ctl = {});

}
}

#endif // HOLLOWINGFILTER_H
