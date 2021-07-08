// paper: https://people.eecs.berkeley.edu/~jrs/meshpapers/GarlandHeckbert2.pdf
// sum up: https://users.csc.calpoly.edu/~zwood/teaching/csc570/final06/jseeba/
// inspiration: https://github.com/sp4cerat/Fast-Quadric-Mesh-Simplification

#include <cstdint>
#include "TriangleMesh.hpp"

namespace Slic3r {

/// <summary>
/// Simplify mesh by Quadric metric
/// </summary>
/// <param name="its">IN/OUT triangle mesh to be simplified.</param>
/// <param name="triangle_count">Wanted triangle count.</param>
/// <param name="max_error">Maximal Quadric for reduce.
/// When nullptr then max float is used
/// Output: Last used ErrorValue to collapse edge
/// </param>
void its_quadric_edge_collapse(indexed_triangle_set &its,
                               uint32_t              triangle_count = 0,
                               float *               max_error = nullptr);

} // namespace Slic3r
