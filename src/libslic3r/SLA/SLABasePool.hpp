#ifndef SLABASEPOOL_HPP
#define SLABASEPOOL_HPP

#include <vector>

namespace Slic3r {

class ExPolygon;
class TriangleMesh;

namespace sla {

using ExPolygons = std::vector<ExPolygon>;

/// Calculate the polygon representing the silhouette from the specified height
void base_plate(const TriangleMesh& mesh,
                ExPolygons& output,
                float height = 0.1f);

struct PoolConfig {
    double min_wall_thickness_mm = 2;
    double min_wall_height_mm = 5;
    double max_merge_distance_mm = 50;
    double edge_radius_mm = 1;
};

/// Calculate the pool for the mesh for SLA printing
void create_base_pool(const ExPolygons& base_plate,
                      TriangleMesh& output_mesh,
                      const PoolConfig& = PoolConfig()
                      );

}

}

#endif // SLABASEPOOL_HPP
