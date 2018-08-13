#ifndef SLASUPPORTPOOL_HPP
#define SLASUPPORTPOOL_HPP

#include <vector>

namespace Slic3r {

class ExPolygon;
class TriangleMesh;

namespace sla {

using ExPolygons = std::vector<ExPolygon>;

/// Calculate the polygon representing the slice of the lowest layer of mesh
void ground_layer(const TriangleMesh& mesh,
                  ExPolygons& output,
                  float height = .1f);

/// Calculate the pool for the mesh for SLA printing
void create_base_pool(const ExPolygons& ground_layer,
                      TriangleMesh& output_mesh);

}

}

#endif // SLASUPPORTPOOL_HPP
