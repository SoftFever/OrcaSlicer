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
    for (size_t i = 0; i < data.vertices.size(); ) {
        size_t j = i;
        for (; j < data.vertices.size() && data.vertices[j].coordIdx != -1; ++ j) ;
        if (i == j)
            continue;
        if (j - i != 3) {
            // Non-triangular faces are not supported as of now.
            return false;
        }
        num_faces ++;
        i = j;
    }

    // Convert ObjData into STL.
    TriangleMesh mesh;
    stl_file &stl = mesh.stl;
    stl.stats.type = inmemory;
    stl.stats.number_of_facets = num_faces;
    stl.stats.original_num_facets = num_faces;
    stl_allocate(&stl);
    size_t i_face = 0;
    for (size_t i = 0; i < data.vertices.size(); ++ i) {
        if (data.vertices[i].coordIdx == -1)
            continue;
        stl_facet &facet = stl.facet_start[i_face ++];
        for (unsigned int v = 0; v < 3; ++ v) {
            const ObjParser::ObjVertex &vertex = data.vertices[i++];
            memcpy(&facet.vertex[v].x, &data.coordinates[vertex.coordIdx*4], 3 * sizeof(float));
            if (vertex.normalIdx != -1)
                memcpy(&facet.normal.x, &data.normals[vertex.normalIdx*3], 3 * sizeof(float));
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
