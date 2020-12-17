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

bool load_obj(const char *path, TriangleMesh *meshptr)
{
    if(meshptr == nullptr) return false;
    
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
    TriangleMesh &mesh = *meshptr;
    stl_file &stl = mesh.stl;
    stl.stats.type = inmemory;
    stl.stats.number_of_facets = uint32_t(num_faces + num_quads);
    stl.stats.original_num_facets = int(num_faces + num_quads);
    // stl_allocate clears all the allocated data to zero, all normals are set to zeros as well.
    stl_allocate(&stl);
    size_t i_face = 0;
    for (size_t i = 0; i < data.vertices.size(); ++ i) {
        if (data.vertices[i].coordIdx == -1)
            continue;
        stl_facet &facet = stl.facet_start[i_face ++];
        size_t     num_normals = 0;
        stl_normal normal(stl_normal::Zero());
        for (unsigned int v = 0; v < 3; ++ v) {
            const ObjParser::ObjVertex &vertex = data.vertices[i++];
            memcpy(facet.vertex[v].data(), &data.coordinates[vertex.coordIdx*4], 3 * sizeof(float));
            if (vertex.normalIdx != -1) {
                normal(0) += data.normals[vertex.normalIdx*3];
                normal(1) += data.normals[vertex.normalIdx*3+1];
                normal(2) += data.normals[vertex.normalIdx*3+2];
                ++ num_normals;
            }
        }
        // Result of obj_parseline() call is not checked, thus not all vertices are necessarily finalized with coord_Idx == -1.
        if (i < data.vertices.size() && data.vertices[i].coordIdx != -1) {
            // This is a quad. Produce the other triangle.
            stl_facet &facet2 = stl.facet_start[i_face++];
            facet2.vertex[0] = facet.vertex[0];
            facet2.vertex[1] = facet.vertex[2];
            const ObjParser::ObjVertex &vertex = data.vertices[i++];
            memcpy(facet2.vertex[2].data(), &data.coordinates[vertex.coordIdx * 4], 3 * sizeof(float));
            if (vertex.normalIdx != -1) {
                normal(0) += data.normals[vertex.normalIdx*3];
                normal(1) += data.normals[vertex.normalIdx*3+1];
                normal(2) += data.normals[vertex.normalIdx*3+2];
                ++ num_normals;
            }
            if (num_normals == 4) {
                // Normalize an average normal of a quad.
                float len = facet.normal.norm();
                if (len > EPSILON) {
                    normal /= len;
                    facet.normal = normal;
                    facet2.normal = normal;
                }
            }
        } else if (num_normals == 3) {
            // Normalize an average normal of a triangle.
            float len = facet.normal.norm();
            if (len > EPSILON)
                facet.normal = normal / len;
        }
    }
    stl_get_size(&stl);
    mesh.repair();
    if (mesh.facets_count() == 0) {
        // die "This OBJ file couldn't be read because it's empty.\n"
        return false;
    }
    
    return true;
}

bool load_obj(const char *path, Model *model, const char *object_name_in)
{
    TriangleMesh mesh;
    
    bool ret = load_obj(path, &mesh);
    
    if (ret) {
        std::string  object_name;
        if (object_name_in == nullptr) {
            const char *last_slash = strrchr(path, DIR_SEPARATOR);
            object_name.assign((last_slash == nullptr) ? path : last_slash + 1);
        } else
           object_name.assign(object_name_in);
    
        model->add_object(object_name.c_str(), path, std::move(mesh));
    }
    
    return ret;
}

bool store_obj(const char *path, TriangleMesh *mesh)
{
    //FIXME returning false even if write failed.
    mesh->WriteOBJFile(path);
    return true;
}

bool store_obj(const char *path, ModelObject *model_object)
{
    TriangleMesh mesh = model_object->mesh();
    return store_obj(path, &mesh);
}

bool store_obj(const char *path, Model *model)
{
    TriangleMesh mesh = model->mesh();
    return store_obj(path, &mesh);
}

}; // namespace Slic3r
