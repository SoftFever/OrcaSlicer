#ifndef SLASUPPORTPOOL_HPP
#define SLASUPPORTPOOL_HPP

#include <vector>

namespace Slic3r {

class ExPolygon;
class TriangleMesh;

namespace sla {

using Mesh3D = TriangleMesh;
using GroundLayer = std::vector<ExPolygon>;

/// Calculate the polygon representing the slice of the lowest layer of mesh
void ground_layer(const Mesh3D& mesh, GroundLayer& output);

/// Calculate the pool for the mesh for SLA printing
void create_base_pool(const GroundLayer& ground_layer, Mesh3D& output_mesh);

}

}

#endif // SLASUPPORTPOOL_HPP
