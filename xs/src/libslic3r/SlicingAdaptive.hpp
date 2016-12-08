// Based on implementation by @platsch

#ifndef slic3r_SlicingAdaptive_hpp_
#define slic3r_SlicingAdaptive_hpp_

#include "Slicing.hpp"
#include "admesh/stl.h"

namespace Slic3r
{

class TriangleMesh;

class SlicingAdaptive
{
public:
	void clear();
	void set_layer_height_range(float min, float max) { m_layer_height_min = min; m_layer_height_max = max; }
	void add_mesh(const TriangleMesh *mesh) { m_meshes.push_back(mesh); }
	void prepare();
	float cusp_height(float z, float cusp_value, int &current_facet);
	float horizontal_facet_distance(float z);

protected:
	float								m_layer_height_min;
	float								m_layer_height_max;
	float								m_max_z;

	std::vector<const TriangleMesh*>	m_meshes;
	// Collected faces of all meshes, sorted by raising Z of the bottom most face.
	std::vector<const stl_facet*>		m_faces;
	// Z component of face normals, normalized.
	std::vector<float>					m_face_normal_z;
};

}; // namespace Slic3r

#endif /* slic3r_SlicingAdaptive_hpp_ */
