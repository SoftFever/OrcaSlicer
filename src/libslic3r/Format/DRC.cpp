#include <string>
#include <utility>
#include <cstring>

#include <draco/compression/decode.h>
#include <draco/io/mesh_io.h>
#include <draco/mesh/mesh.h>
using namespace draco;

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "DRC.hpp"

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

namespace Slic3r {

bool load_drc(const char *path, TriangleMesh *meshptr)
{
    struct stat st;
    if (stat(path, &st)) {
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == nullptr) {
        return false;
    }

    char *data = new char[st.st_size];
    if (fread(data, 1, st.st_size, fp) != st.st_size) {
        fclose(fp);
        delete[] data;
        return false;
    }
    fclose(fp);
    
    DecoderBuffer buffer;
    buffer.Init(data, st.st_size);

    auto geotype = Decoder::GetEncodedGeometryType(&buffer);
    if ((!geotype.ok()) || geotype.value() != TRIANGULAR_MESH) {
        delete[] data;
        return false;
    }
    
    Decoder decoder;
    Mesh dracoMesh;
    Status status = decoder.DecodeBufferToGeometry(&buffer, &dracoMesh);
    delete[] data;
    if (!status.ok()) {
        return false;
    }
    
    indexed_triangle_set its;
    
    const PointAttribute *const positions = dracoMesh.GetNamedAttribute(GeometryAttribute::POSITION);    
    size_t num_vertices = positions->size();
    its.vertices.reserve(num_vertices);
    for (AttributeValueIndex i(0); i < num_vertices; ++ i) {
        float pos[3];
        positions->ConvertValue<float>(i, 3, pos);
        its.vertices.emplace_back(pos[0], pos[1], pos[2]);
    }
    
    size_t num_faces = dracoMesh.num_faces();
    its.indices.reserve(num_faces);
    for (FaceIndex i(0); i < num_faces; ++ i) {
        Mesh::Face face = dracoMesh.face(i);
        
        its.indices.emplace_back(
            positions->mapped_index(face[0]).value(),
            positions->mapped_index(face[1]).value(),
            positions->mapped_index(face[2]).value()
        );
    }

    *meshptr = TriangleMesh(std::move(its));
    if (meshptr->volume() < 0)
        meshptr->flip_triangles();
    return true;
}

bool load_drc(const char *path, Model *model, const char *object_name_in)
{
    TriangleMesh mesh;
    
    bool ret = load_drc(path, &mesh);
    
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

bool store_drc(const char *path, TriangleMesh *mesh, bool binary)
{
    // TODO
    return false;
}

bool store_drc(const char *path, ModelObject *model_object, bool binary)
{
    TriangleMesh mesh = model_object->mesh();
    return store_drc(path, &mesh, binary);
}

bool store_drc(const char *path, Model *model, bool binary)
{
    TriangleMesh mesh = model->mesh();
    return store_drc(path, &mesh, binary);
}

}; // namespace Slic3r
