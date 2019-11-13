// Based on implementation by @platsch

#ifndef slic3r_SlicingAdaptive_hpp_
#define slic3r_SlicingAdaptive_hpp_

#include "Slicing.hpp"
#include "admesh/stl.h"
#if ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE
#include "TriangleMesh.hpp"
#endif // ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE

namespace Slic3r
{

#if ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE
class ModelVolume;
#else
class TriangleMesh;
#endif // ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE

class SlicingAdaptive
{
public:
#if !ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE
    void clear();
#endif // !ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE
    void set_slicing_parameters(SlicingParameters params) { m_slicing_params = params; }
#if ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE
    void set_object(const ModelObject& object) { m_object = &object; }
#else
    void add_mesh(const TriangleMesh* mesh) { m_meshes.push_back(mesh); }
#endif // ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE
    void prepare();
	float cusp_height(float z, float cusp_value, int &current_facet);
#if !ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE
    float horizontal_facet_distance(float z);
#endif // !ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE

protected:
	SlicingParameters 					m_slicing_params;

#if ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE
    const ModelObject*                  m_object;
    TriangleMesh                        m_mesh;
#else
    std::vector<const TriangleMesh*>	m_meshes;
#endif // ENABLE_ADAPTIVE_LAYER_HEIGHT_PROFILE
    // Collected faces of all meshes, sorted by raising Z of the bottom most face.
	std::vector<const stl_facet*>		m_faces;
    // Z component of face normals, normalized.
	std::vector<float>					m_face_normal_z;
};

}; // namespace Slic3r

#endif /* slic3r_SlicingAdaptive_hpp_ */
