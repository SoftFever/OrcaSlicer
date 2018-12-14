#ifndef SLAAUTOSUPPORTS_HPP_
#define SLAAUTOSUPPORTS_HPP_

#include <libslic3r/Point.hpp>
#include <libslic3r/TriangleMesh.hpp>



namespace Slic3r {

class ModelObject;

namespace SLAAutoSupports {


class SLAAutoSupports {
public:
    struct Config {
            float density_at_horizontal;
            float density_at_45;
            float minimal_z;
        };

    SLAAutoSupports(ModelObject& mo, const SLAAutoSupports::Config& c);
    void generate();

private:
    TriangleMesh mesh;
    static float angle_from_normal(const stl_normal& normal) { return acos((-normal.normalized())(2)); }
    float get_required_density(float angle) const;
    static float approximate_geodesic_distance(const Vec3f& p1, const Vec3f& p2, Vec3f& n1, Vec3f& n2);

    ModelObject& m_model_object;
    SLAAutoSupports::Config m_config;
};


std::vector<Vec3d> find_islands(const std::vector<ExPolygons>& slices, const std::vector<float>& heights);

} // namespace SLAAutoSupports

} // namespace Slic3r


#endif // SLAAUTOSUPPORTS_HPP_