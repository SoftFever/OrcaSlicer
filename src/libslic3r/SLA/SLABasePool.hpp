#ifndef SLABASEPOOL_HPP
#define SLABASEPOOL_HPP

#include <vector>
#include <functional>

namespace Slic3r {

class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;

class TriangleMesh;

namespace sla {

using ThrowOnCancel = std::function<void(void)>;

/// Calculate the polygon representing the silhouette from the specified height
void base_plate(const TriangleMesh& mesh,       // input mesh
                ExPolygons& output,             // Output will be merged with
                float zlevel = 0.1f,            // Plate creation level
                float layerheight = 0.05f,      // The sampling height
                ThrowOnCancel thrfn = [](){});  // Will be called frequently

struct PoolConfig {
    double min_wall_thickness_mm = 2;
    double min_wall_height_mm = 5;
    double max_merge_distance_mm = 50;
    double edge_radius_mm = 1;

    ThrowOnCancel throw_on_cancel = [](){};

    inline PoolConfig() {}
    inline PoolConfig(double wt, double wh, double md, double er):
        min_wall_thickness_mm(wt),
        min_wall_height_mm(wh),
        max_merge_distance_mm(md),
        edge_radius_mm(er) {}
};

/// Calculate the pool for the mesh for SLA printing
void create_base_pool(const ExPolygons& base_plate,
                      TriangleMesh& output_mesh,
                      const PoolConfig& = PoolConfig());

/// TODO: Currently the base plate of the pool will have half the height of the
/// whole pool. So the carved out space has also half the height. This is not
/// a particularly elegant solution, the thickness should be exactly
/// min_wall_thickness and it should be corrected in the future. This method
/// will return the correct value for further processing.
inline double get_pad_elevation(const PoolConfig& cfg) {
    return cfg.min_wall_height_mm / 2.0;
}

}

}

#endif // SLABASEPOOL_HPP
