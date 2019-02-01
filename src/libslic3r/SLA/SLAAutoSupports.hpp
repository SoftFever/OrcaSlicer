#ifndef SLAAUTOSUPPORTS_HPP_
#define SLAAUTOSUPPORTS_HPP_

#include <libslic3r/Point.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/SLACommon.hpp>

// #define SLA_AUTOSUPPORTS_DEBUG

namespace Slic3r {

class SLAAutoSupports {
public:
    struct Config {
            float density_at_horizontal;
            float density_at_45;
            float minimal_z;
            ///////////////
            float support_force = 30; // a force one point can support       (arbitrary force unit)
            float tear_pressure = 1; // pressure that the display exerts    (the force unit per mm2)
        };

    SLAAutoSupports(const TriangleMesh& mesh, const sla::EigenMesh3D& emesh, const std::vector<ExPolygons>& slices,
                     const std::vector<float>& heights, const Config& config, std::function<void(void)> throw_on_cancel);
    const std::vector<sla::SupportPoint>& output() { return m_output; }

private:
    std::vector<sla::SupportPoint> m_output;

#ifdef SLA_AUTOSUPPORTS_DEBUG
    void output_expolygons(const ExPolygons& expolys, std::string filename) const;
    void output_structures() const;
#endif /* SLA_AUTOSUPPORTS_DEBUG */

    SLAAutoSupports::Config m_config;

    struct Structure {
        Structure(const ExPolygon& poly, float h) : height(h), unique_id(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())) {
            polygon = &poly;
        }
        const ExPolygon* polygon = nullptr;
        std::vector<const Structure*> structures_below;
        float height = 0;
        float supports_force = 0.f;
        std::chrono::milliseconds unique_id;
    };
    std::vector<Structure> m_structures_old;
    std::vector<Structure> m_structures_new;
    float m_supports_force_total = 0.f;

    void process(const std::vector<ExPolygons>& slices, const std::vector<float>& heights);
    void add_point(const Point& point, Structure& structure, bool island  = false);
    void uniformly_cover(const ExPolygon& island, Structure& structure, bool is_new_island = false, bool just_one = false);
    void project_onto_mesh(std::vector<sla::SupportPoint>& points) const;


    std::function<void(void)> m_throw_on_cancel;
    const Eigen::MatrixXd& m_V;
    const Eigen::MatrixXi& m_F;
};


} // namespace Slic3r


#endif // SLAAUTOSUPPORTS_HPP_