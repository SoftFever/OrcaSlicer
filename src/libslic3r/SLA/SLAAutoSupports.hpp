#ifndef SLAAUTOSUPPORTS_HPP_
#define SLAAUTOSUPPORTS_HPP_

#include <libslic3r/Point.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/SLASupportTree.hpp>

// #define SLA_AUTOSUPPORTS_DEBUG

namespace Slic3r {

class SLAAutoSupports {
public:
    struct Config {
            float density_at_horizontal;
            float density_at_45;
            float minimal_z;
        };

    SLAAutoSupports(const TriangleMesh& mesh, const sla::EigenMesh3D& emesh, const std::vector<ExPolygons>& slices, const std::vector<float>& heights, const Config& config);
    const std::vector<Vec3d>& output() { return m_output; }

private:
    std::vector<Vec3d> m_output;
    std::vector<Vec3d> m_normals;
    TriangleMesh mesh;
    static float angle_from_normal(const stl_normal& normal) { return acos((-normal.normalized())(2)); }
    float get_required_density(float angle) const;
    float distance_limit(float angle) const;
    static float approximate_geodesic_distance(const Vec3d& p1, const Vec3d& p2, Vec3d& n1, Vec3d& n2);
    std::vector<std::pair<ExPolygon, coord_t>> find_islands(const std::vector<ExPolygons>& slices, const std::vector<float>& heights) const;
    void sprinkle_mesh(const TriangleMesh& mesh);
    std::vector<Vec3d> uniformly_cover(const std::pair<ExPolygon, coord_t>& island);
    void project_upward_onto_mesh(std::vector<Vec3d>& points) const;

#ifdef SLA_AUTOSUPPORTS_DEBUG
    void output_expolygons(const ExPolygons& expolys, std::string filename) const;
#endif /* SLA_AUTOSUPPORTS_DEBUG */

    SLAAutoSupports::Config m_config;
    const Eigen::MatrixXd& m_V;
    const Eigen::MatrixXi& m_F;
};


} // namespace Slic3r


#endif // SLAAUTOSUPPORTS_HPP_