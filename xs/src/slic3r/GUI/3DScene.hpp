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

class GLVertexArray {
public:
    GLVertexArray() {}
    GLVertexArray(const GLVertexArray &rhs) : verts(rhs.verts), norms(rhs.norms) {}
    GLVertexArray(GLVertexArray &&rhs) : verts(std::move(rhs.verts)), norms(std::move(rhs.norms)) {}

    GLVertexArray& operator=(const GLVertexArray &rhs) { verts = rhs.verts; norms = rhs.norms; return *this; }
    GLVertexArray& operator=(GLVertexArray &&rhs) { verts = std::move(rhs.verts); norms = std::move(rhs.norms); return *this; }

    std::vector<float> verts, norms;
    
    void reserve(size_t len) {
        this->verts.reserve(len);
        this->norms.reserve(len);
    };
    void reserve_more(size_t len) {
        len += this->verts.size();
        this->reserve(len);
    };
    void push_vert(const Pointf3 &point) {
        this->verts.push_back(point.x);
        this->verts.push_back(point.y);
        this->verts.push_back(point.z);
    };
    void push_vert(float x, float y, float z) {
        this->verts.push_back(x);
        this->verts.push_back(y);
        this->verts.push_back(z);
    };
    void push_norm(const Pointf3 &point) {
        this->norms.push_back(point.x);
        this->norms.push_back(point.y);
        this->norms.push_back(point.z);
    };
    void push_norm(float x, float y, float z) {
        this->norms.push_back(x);
        this->norms.push_back(y);
        this->norms.push_back(z);
    };
    void load_mesh(const TriangleMesh &mesh);

    size_t size() const { return verts.size(); }
    bool empty() const { return verts.empty(); }
    void shrink_to_fit() { this->verts.shrink_to_fit(); this->norms.shrink_to_fit(); }

    BoundingBoxf3 bounding_box() const {
        BoundingBoxf3 bbox;
        if (! this->verts.empty()) {
            bbox.min.x = bbox.max.x = this->verts[0];
            bbox.min.y = bbox.max.y = this->verts[1];
            bbox.min.z = bbox.max.z = this->verts[2];
            for (size_t i = 3; i < this->verts.size(); i += 3) {
                bbox.min.x = std::min<coordf_t>(bbox.min.x, this->verts[i + 0]);
                bbox.min.y = std::min<coordf_t>(bbox.min.y, this->verts[i + 1]);
                bbox.min.z = std::min<coordf_t>(bbox.min.z, this->verts[i + 2]);
                bbox.max.x = std::max<coordf_t>(bbox.max.x, this->verts[i + 0]);
                bbox.max.y = std::max<coordf_t>(bbox.max.y, this->verts[i + 1]);
                bbox.max.z = std::max<coordf_t>(bbox.max.z, this->verts[i + 2]);
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
        tverts_range(0, size_t(-1))
    {
        color[0] = r;
        color[1] = g;
        color[2] = b;
        color[3] = a;
    }
    GLVolume(const float *rgba) : GLVolume(rgba[0], rgba[1], rgba[2], rgba[3]) {}

    std::vector<int> load_object(
        const ModelObject       *model_object, 
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

    // Geometric data.
    // Quad vertices.
    GLVertexArray               qverts;
    std::pair<size_t, size_t>   qverts_range;
    // Triangle vertices.
    GLVertexArray               tverts;
    std::pair<size_t, size_t>   tverts_range;
    // If the qverts or tverts contain thick extrusions, then offsets keeps pointers of the starts
    // of the extrusions per layer.
    // The offsets stores tripples of (z_top, qverts_idx, tverts_idx) in a linear array.
    std::vector<coordf_t>       print_zs;
    std::vector<size_t>         offsets;

    int object_idx() const { return this->composite_id / 1000000; }
    int volume_idx() const { return (this->composite_id / 1000) % 1000; }
    int instance_idx() const { return this->composite_id % 1000; }
    BoundingBoxf3 transformed_bounding_box() const { BoundingBoxf3 bb = this->bounding_box; bb.translate(this->origin); return bb; }
    bool empty() const { return qverts.size() < 4 && tverts.size() < 3; }

    void set_range(coordf_t low, coordf_t high);

    void*               qverts_to_render_ptr() { return qverts.verts.data() + qverts_range.first; }
    void*               qnorms_to_render_ptr() { return qverts.norms.data() + qverts_range.first; }
    size_t              qverts_to_render_cnt() { return std::min(qverts.verts.size(), qverts_range.second - qverts_range.first); }
    void*               tverts_to_render_ptr() { return tverts.verts.data() + tverts_range.first; }
    void*               tnorms_to_render_ptr() { return tverts.norms.data() + tverts_range.first; }
    size_t              tverts_to_render_cnt() { return std::min(tverts.verts.size(), tverts_range.second - tverts_range.first); }

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
};

class _3DScene
{
public:
    static void _load_print_toolpaths(
        const Print         *print,
        GLVolumeCollection  *volumes);

    static void _load_print_object_toolpaths(
        const PrintObject   *print_object,
        GLVolumeCollection  *volumes);
};

}

#endif
