#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"

#include "OBJ.hpp"
#include "objparser.hpp"

#include <string>

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

namespace Slic3r {

bool load_obj(const char *path, Model *model, const char *object_name_in)
{
    // Parse the OBJ file.
    ObjParser::ObjData data;
    if (! ObjParser::objparse(path, data)) {
//    die "Failed to parse $file\n" if !-e $path;
        return false;
    }

    // Count the faces and verify, that all faces are triangular.
    size_t num_faces = 0;
	size_t num_quads = 0;
    for (size_t i = 0; i < data.vertices.size(); ) {
        size_t j = i;
        for (; j < data.vertices.size() && data.vertices[j].coordIdx != -1; ++ j) ;
        if (i == j)
            continue;
		size_t face_vertices = j - i;
		if (face_vertices != 3 && face_vertices != 4) {
            // Non-triangular and non-quad faces are not supported as of now.
            return false;
        }
		if (face_vertices == 4)
			++ num_quads;
		++ num_faces;
        i = j + 1;
    }

    // Convert ObjData into STL.
    TriangleMesh mesh;
    stl_file &stl = mesh.stl;
    stl.stats.type = inmemory;
    stl.stats.number_of_facets = int(num_faces + num_quads);
    stl.stats.original_num_facets = int(num_faces + num_quads);
    // stl_allocate clears all the allocated data to zero, all normals are set to zeros as well.
    stl_allocate(&stl);
    size_t i_face = 0;
    for (size_t i = 0; i < data.vertices.size(); ++ i) {
        if (data.vertices[i].coordIdx == -1)
            continue;
        stl_facet &facet = stl.facet_start[i_face ++];
        size_t     num_normals = 0;
        stl_normal normal = { 0.f };
        for (unsigned int v = 0; v < 3; ++ v) {
            const ObjParser::ObjVertex &vertex = data.vertices[i++];
            memcpy(&facet.vertex[v].x, &data.coordinates[vertex.coordIdx*4], 3 * sizeof(float));
            if (vertex.normalIdx != -1) {
                normal.x += data.normals[vertex.normalIdx*3];
                normal.y += data.normals[vertex.normalIdx*3+1];
                normal.z += data.normals[vertex.normalIdx*3+2];
                ++ num_normals;
            }
        }
		if (data.vertices[i].coordIdx != -1) {
			// This is a quad. Produce the other triangle.
			stl_facet &facet2 = stl.facet_start[i_face++];
            facet2.vertex[0] = facet.vertex[0];
            facet2.vertex[1] = facet.vertex[2];
			const ObjParser::ObjVertex &vertex = data.vertices[i++];
			memcpy(&facet2.vertex[2].x, &data.coordinates[vertex.coordIdx * 4], 3 * sizeof(float));
			if (vertex.normalIdx != -1) {
                normal.x += data.normals[vertex.normalIdx*3];
                normal.y += data.normals[vertex.normalIdx*3+1];
                normal.z += data.normals[vertex.normalIdx*3+2];
                ++ num_normals;
            }
            if (num_normals == 4) {
                // Normalize an average normal of a quad.
                float len = sqrt(facet.normal.x*facet.normal.x + facet.normal.y*facet.normal.y + facet.normal.z*facet.normal.z);
                if (len > EPSILON) {
                    normal.x /= len;
                    normal.y /= len;
                    normal.z /= len;
                    facet.normal = normal;
                    facet2.normal = normal;
                }
            }
        } else if (num_normals == 3) {
            // Normalize an average normal of a triangle.
            float len = sqrt(facet.normal.x*facet.normal.x + facet.normal.y*facet.normal.y + facet.normal.z*facet.normal.z);
            if (len > EPSILON) {
                normal.x /= len;
                normal.y /= len;
                normal.z /= len;
                facet.normal = normal;
            }
        }
	}
    stl_get_size(&stl);
    mesh.repair();
    if (mesh.facets_count() == 0) {
        // die "This STL file couldn't be read because it's empty.\n"
        return false;
    }

    std::string  object_name;
    if (object_name_in == nullptr) {
        const char *last_slash = strrchr(path, DIR_SEPARATOR);
        object_name.assign((last_slash == nullptr) ? path : last_slash + 1);
    } else
       object_name.assign(object_name_in);

    model->add_object(object_name.c_str(), path, std::move(mesh));
    return true;
}

}; // namespace Slic3r
