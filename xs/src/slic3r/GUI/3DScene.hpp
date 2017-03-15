#ifndef slic3r_3DScene_hpp_
#define slic3r_3DScene_hpp_

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/Point.hpp"
#include "../../libslic3r/Line.hpp"
#include "../../libslic3r/TriangleMesh.hpp"

namespace Slic3r {

class Print;
class PrintObject;
class Model;
class ModelObject;

// A container for interleaved arrays of 3D vertices and normals,
// possibly indexed by triangles and / or quads.
class GLIndexedVertexArray {
public:
    GLIndexedVertexArray() {}
    GLIndexedVertexArray(const GLIndexedVertexArray &rhs) :
        vertices_and_normals_interleaved(rhs.vertices_and_normals_interleaved),
        triangle_indices(rhs.triangle_indices),
        quad_indices(rhs.quad_indices)
        {}
    GLIndexedVertexArray(GLIndexedVertexArray &&rhs) :
        vertices_and_normals_interleaved(std::move(rhs.vertices_and_normals_interleaved)),
        triangle_indices(std::move(rhs.triangle_indices)),
        quad_indices(std::move(rhs.quad_indices))
        {}

    GLIndexedVertexArray& operator=(const GLIndexedVertexArray &rhs)
    {
        this->vertices_and_normals_interleaved = rhs.vertices_and_normals_interleaved;
        this->triangle_indices                 = rhs.triangle_indices;
        this->quad_indices                     = rhs.quad_indices;
        return *this;
    }

    GLIndexedVertexArray& operator=(GLIndexedVertexArray &&rhs) 
    {
        this->vertices_and_normals_interleaved = std::move(rhs.vertices_and_normals_interleaved);
        this->triangle_indices                 = std::move(rhs.triangle_indices);
        this->quad_indices                     = std::move(rhs.quad_indices);
        return *this;
    }

    // Vertices and their normals, interleaved to be used by void glInterleavedArrays(GL_N3F_V3F, 0, x)
    std::vector<float> vertices_and_normals_interleaved;
    std::vector<int>   triangle_indices;
    std::vector<int>   quad_indices;

    void load_mesh_flat_shading(const TriangleMesh &mesh);

    inline void reserve(size_t sz) {
        this->vertices_and_normals_interleaved.reserve(sz * 6);
        this->triangle_indices.reserve(sz * 3);
        this->quad_indices.reserve(sz * 4);
    }

    inline void push_geometry(float x, float y, float z, float nx, float ny, float nz) {
        this->vertices_and_normals_interleaved.reserve(this->vertices_and_normals_interleaved.size() + 6);
        this->vertices_and_normals_interleaved.push_back(nx);
        this->vertices_and_normals_interleaved.push_back(ny);
        this->vertices_and_normals_interleaved.push_back(nz);
        this->vertices_and_normals_interleaved.push_back(x);
        this->vertices_and_normals_interleaved.push_back(y);
        this->vertices_and_normals_interleaved.push_back(z);
    };

    inline void push_geometry(double x, double y, double z, double nx, double ny, double nz) {
        push_geometry(float(x), float(y), float(z), float(nx), float(ny), float(nz));
    }

    // Is there any geometry data stored?
    bool empty() const { return vertices_and_normals_interleaved.empty(); }

    // Is this object indexed, or is it just a set of triangles?
    bool indexed() const { return ! this->empty() && (! this->triangle_indices.empty() || ! this->quad_indices.empty()); }

    void clear() {
        this->vertices_and_normals_interleaved.clear();
        this->triangle_indices.clear();
        this->quad_indices.clear(); 
    }

    // Shrink the internal storage to tighly fit the data stored.
    void shrink_to_fit() { 
        this->vertices_and_normals_interleaved.shrink_to_fit();
        this->triangle_indices.shrink_to_fit();
        this->quad_indices.shrink_to_fit(); 
    }

    BoundingBoxf3 bounding_box() const {
        BoundingBoxf3 bbox;
        if (! this->vertices_and_normals_interleaved.empty()) {
            bbox.min.x = bbox.max.x = this->vertices_and_normals_interleaved[3];
            bbox.min.y = bbox.max.y = this->vertices_and_normals_interleaved[4];
            bbox.min.z = bbox.max.z = this->vertices_and_normals_interleaved[5];
            for (size_t i = 9; i < this->vertices_and_normals_interleaved.size(); i += 6) {
                const float *verts = this->vertices_and_normals_interleaved.data() + i;
                bbox.min.x = std::min<coordf_t>(bbox.min.x, verts[0]);
                bbox.min.y = std::min<coordf_t>(bbox.min.y, verts[1]);
                bbox.min.z = std::min<coordf_t>(bbox.min.z, verts[2]);
                bbox.max.x = std::max<coordf_t>(bbox.max.x, verts[0]);
                bbox.max.y = std::max<coordf_t>(bbox.max.y, verts[1]);
                bbox.max.z = std::max<coordf_t>(bbox.max.z, verts[2]);
            }
        }
        return bbox;
    }
};

class GLTexture
{
public:
    GLTexture() : width(0), height(0), levels(0), cells(0) {}

    // Texture data
    std::vector<char>   data;
    // Width of the texture, top level.
    size_t              width;
    // Height of the texture, top level.
    size_t              height;
    // For how many levels of detail is the data allocated?
    size_t              levels;
    // Number of texture cells allocated for the height texture.
    size_t              cells;
};

class GLVolume {
public:
    GLVolume(float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f) :
        composite_id(-1),
        select_group_id(-1),
        drag_group_id(-1),
        selected(false),
        hover(false),
        qverts_range(0, size_t(-1)),
        tverts_range(0, size_t(-1)),
        name_vertex_buffer(0),
        name_normal_buffer(0),
        name_index_buffer(0)
    {
        color[0] = r;
        color[1] = g;
        color[2] = b;
        color[3] = a;
    }
    GLVolume(const float *rgba) : GLVolume(rgba[0], rgba[1], rgba[2], rgba[3]) {}

    std::vector<int> load_object(
        const ModelObject        *model_object, 
        const std::vector<int>   &instance_idxs,
        const std::string        &color_by,
        const std::string        &select_by,
        const std::string        &drag_by);

    // Bounding box of this volume, in unscaled coordinates.
    BoundingBoxf3       bounding_box;
    // Offset of the volume to be rendered.
    Pointf3             origin;
    // Color of the triangles / quads held by this volume.
    float               color[4];

    // An ID containing the object ID, volume ID and instance ID.
    int                 composite_id;
    // An ID for group selection. It may be the same for all meshes of all object instances, or for just a single object instance.
    int                 select_group_id;
    // An ID for group dragging. It may be the same for all meshes of all object instances, or for just a single object instance.
    int                 drag_group_id;
    // Is this object selected?
    bool                selected;
    // Boolean: Is mouse over this object?
    bool                hover;

    // Interleaved triangles & normals with indexed triangles & quads.
    GLIndexedVertexArray        indexed_vertex_array;
    // Ranges of triangle and quad indices to be rendered.
    std::pair<size_t, size_t>   tverts_range;
    std::pair<size_t, size_t>   qverts_range;

    // If the qverts or tverts contain thick extrusions, then offsets keeps pointers of the starts
    // of the extrusions per layer.
    std::vector<coordf_t>       print_zs;
    // Offset into qverts & tverts, or offsets into indices stored into an OpenGL name_index_buffer.
    std::vector<size_t>         offsets;

    // OpenGL buffers for vertices and their normals.
    int                         name_vertex_buffer;
    int                         name_normal_buffer;
    // OpenGL buffer of the indices.
    int                         name_index_buffer;

    int                 object_idx() const { return this->composite_id / 1000000; }
    int                 volume_idx() const { return (this->composite_id / 1000) % 1000; }
    int                 instance_idx() const { return this->composite_id % 1000; }
    BoundingBoxf3       transformed_bounding_box() const { BoundingBoxf3 bb = this->bounding_box; bb.translate(this->origin); return bb; }

    bool                empty() const { return this->indexed_vertex_array.empty(); }
    bool                indexed() const { return this->indexed_vertex_array.indexed(); }

    void                set_range(coordf_t low, coordf_t high);

    // Non-indexed interleaved vertices & normals, likely forming triangles.
    void*               triangles_to_render_ptr() { return indexed_vertex_array.vertices_and_normals_interleaved.data(); }
    size_t              triangles_to_render_cnt() { return indexed_vertex_array.vertices_and_normals_interleaved.size() / (3 * 2); }
    // Indexed triangles & quads, complete set for storing into a vertex buffer.
    size_t              geometry_size() const { return indexed_vertex_array.vertices_and_normals_interleaved.size() * 4; }
    void*               triangle_indices_ptr() { return indexed_vertex_array.triangle_indices.data(); }
    void*               quad_indices_ptr() { return indexed_vertex_array.quad_indices.data(); }
    size_t              indexed_triangles_cnt() { return indexed_vertex_array.triangle_indices.size(); }
    size_t              indexed_quads_cnt() { return indexed_vertex_array.quad_indices.size(); }
    // Indexed triangles & quads, to be painted in an immediate mode.
    size_t              triangle_indices_to_render_offset() const { return tverts_range.first; }
    size_t              quad_indices_to_render_offset() const { return qverts_range.first; }
    size_t              indexed_triangles_to_render_cnt() const { return std::min(indexed_vertex_array.triangle_indices.size(), tverts_range.second - tverts_range.first); }
    size_t              indexed_quads_to_render_cnt() const { return std::min(indexed_vertex_array.quad_indices.size(), qverts_range.second - qverts_range.first); }

    void                render_VBOs() const;

    /************************************************ Layer height texture ****************************************************/
    std::shared_ptr<GLTexture>  layer_height_texture;

    bool                has_layer_height_texture() const 
        { return this->layer_height_texture.get() != nullptr; }
    size_t              layer_height_texture_width() const 
        { return (this->layer_height_texture.get() == nullptr) ? 0 : this->layer_height_texture->width; }
    size_t              layer_height_texture_height() const 
        { return (this->layer_height_texture.get() == nullptr) ? 0 : this->layer_height_texture->height; }
    size_t              layer_height_texture_cells() const 
        { return (this->layer_height_texture.get() == nullptr) ? 0 : this->layer_height_texture->cells; }
    void*               layer_height_texture_data_ptr_level0() {
        return (layer_height_texture.get() == nullptr) ? 0 :
            (void*)layer_height_texture->data.data();
    }
    void*               layer_height_texture_data_ptr_level1() {
        return (layer_height_texture.get() == nullptr) ? 0 :
            (void*)(layer_height_texture->data.data() + layer_height_texture->width * layer_height_texture->height * 4);
    }
    double              layer_height_texture_z_to_row_id() const { 
        return (this->layer_height_texture.get() == nullptr) ? 0. : 
            double(this->layer_height_texture->cells - 1) / (double(this->layer_height_texture->width) * bounding_box.max.z);
    }
    void                generate_layer_height_texture(PrintObject *print_object, bool force);
};

class GLVolumeCollection
{
public:
    std::vector<GLVolume*> volumes;
    
    GLVolumeCollection() {};
    ~GLVolumeCollection() { clear(); };

    std::vector<int> load_object(
        const ModelObject       *model_object,
        int                      obj_idx,
        const std::vector<int>  &instance_idxs,
        const std::string       &color_by,
        const std::string       &select_by,
        const std::string       &drag_by);

    void clear() { for (auto *v : volumes) delete v; volumes.clear(); }
    bool empty() const { return volumes.empty(); }
    void set_range(double low, double high) { for (GLVolume *vol : this->volumes) vol->set_range(low, high); }
    void render_VBOs() const { for (GLVolume *vol : this->volumes) vol->render_VBOs(); }

private:
    GLVolumeCollection(const GLVolumeCollection &other);
    GLVolumeCollection& operator=(const GLVolumeCollection &);
};

class _3DScene
{
public:
    static void _load_print_toolpaths(
        const Print         *print,
        GLVolumeCollection  *volumes,
        bool                 use_VBOs);

    static void _load_print_object_toolpaths(
        const PrintObject   *print_object,
        GLVolumeCollection  *volumes,
        bool                 use_VBOs);
};

}

#endif
