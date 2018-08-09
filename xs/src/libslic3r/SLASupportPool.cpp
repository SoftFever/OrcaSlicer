
#include "SLASupportPool.hpp"
#include "ExPolygon.hpp"
#include "TriangleMesh.hpp"
#include "boost/geometry.hpp"

namespace Slic3r { namespace sla {

void ground_layer(const Mesh3D &mesh, GroundLayer &output)
{
    Mesh3D m = mesh;
    TriangleMeshSlicer slicer(&m);

    std::vector<GroundLayer> tmp;

    slicer.slice({0.1f}, &tmp);

    output = tmp.front();
}

void create_base_pool(const GroundLayer &ground_layer, Mesh3D& out)
{
    // 1: Offset the ground layer
    ExPolygon in;
    ExPolygon chull;
    boost::geometry::convex_hull(in, chull);

    // 2: triangulate the ground layer

}

}
}
