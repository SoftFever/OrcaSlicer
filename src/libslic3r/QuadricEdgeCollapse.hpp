// paper: https://people.eecs.berkeley.edu/~jrs/meshpapers/GarlandHeckbert2.pdf
// sum up: https://users.csc.calpoly.edu/~zwood/teaching/csc570/final06/jseeba/
// inspiration: https://github.com/sp4cerat/Fast-Quadric-Mesh-Simplification

#include "TriangleMesh.hpp"

namespace Slic3r {

/// <summary>
/// Simplify mesh by Quadric metric
/// </summary>
/// <param name="its">IN/OUT triangle mesh to be simplified.</param>
/// <param name="triangle_count">wanted triangle count.</param>
/// <returns>TRUE on success otherwise FALSE</returns>
bool its_quadric_edge_collapse(indexed_triangle_set &its, size_t triangle_count);

} // namespace Slic3r
