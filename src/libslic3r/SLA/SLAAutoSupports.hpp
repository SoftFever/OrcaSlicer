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
            float support_force = 30.f; // a force one point can support       (arbitrary force unit)
            float tear_pressure = 1.f; // pressure that the display exerts    (the force unit per mm2)
        };

    SLAAutoSupports(const TriangleMesh& mesh, const sla::EigenMesh3D& emesh, const std::vector<ExPolygons>& slices,
                     const std::vector<float>& heights, const Config& config, std::function<void(void)> throw_on_cancel);
    const std::vector<sla::SupportPoint>& output() { return m_output; }

private:
    std::vector<sla::SupportPoint> m_output;

    SLAAutoSupports::Config m_config;

    struct Structure {
        Structure(const ExPolygon& poly, const BoundingBox &bbox, const Vec2f &centroid, float area, float h) : polygon(&poly), bbox(bbox), centroid(centroid), area(area), height(h)
#ifdef SLA_AUTOSUPPORTS_DEBUG
            , unique_id(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()))
#endif /* SLA_AUTOSUPPORTS_DEBUG */
            {}
        const ExPolygon* polygon = nullptr;
        const BoundingBox bbox;
        const Vec2f centroid = Vec3f::Zero();
        const float area = 0.f;
        std::vector<const Structure*> structures_below;
        float height = 0;
        // How well is this ExPolygon held to the print base?
        // Positive number, the higher the better.
        float supports_force = 0.f;
#ifdef SLA_AUTOSUPPORTS_DEBUG
        std::chrono::milliseconds unique_id;
#endif /* SLA_AUTOSUPPORTS_DEBUG */

        bool overlaps(const Structure &rhs) const { return this->bbox.overlap(rhs.bbox) && (this->polygon->overlaps(*rhs.polygon) || rhs.polygon->overlaps(*this->polygon)); }
        float area_below() const { 
            float area = 0.f; 
            for (const Structure *below : this->structures_below) 
                area += below->area; 
            return area;
        }
        Polygons polygons_below() const { 
            size_t cnt = 0;
            for (const Structure *below : this->structures_below)
                cnt += 1 + below->polygon->holes.size();
            Polygons out;
            out.reserve(cnt);
            for (const Structure *below : this->structures_below) {
                out.emplace_back(below->polygon->contour);
                append(out, below->polygon->holes);
            }
            return out;
        }
        ExPolygons expolygons_below() const { 
            ExPolygons out;
			out.reserve(this->structures_below.size());
            for (const Structure *below : this->structures_below)
                out.emplace_back(*below->polygon);
            return out;
        }
    };

    float m_supports_force_total = 0.f;

    void process(const std::vector<ExPolygons>& slices, const std::vector<float>& heights);
    void uniformly_cover(const ExPolygon& island, Structure& structure, bool is_new_island = false, bool just_one = false);
    void project_onto_mesh(std::vector<sla::SupportPoint>& points) const;

#ifdef SLA_AUTOSUPPORTS_DEBUG
    static void output_expolygons(const ExPolygons& expolys, const std::string &filename);
    static void output_structures(const std::vector<Structure> &structures);
#endif // SLA_AUTOSUPPORTS_DEBUG

    std::function<void(void)> m_throw_on_cancel;
    const sla::EigenMesh3D& m_emesh;
};


} // namespace Slic3r


#endif // SLAAUTOSUPPORTS_HPP_