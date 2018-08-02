#ifndef slic3r_3DScene_hpp_
#define slic3r_3DScene_hpp_

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/Point.hpp"
#include "../../libslic3r/Line.hpp"
#include "../../libslic3r/TriangleMesh.hpp"
#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/Model.hpp"
#include "../../slic3r/GUI/GLCanvas3DManager.hpp"

class wxBitmap;
class wxWindow;

namespace Slic3r {

class Print;
class PrintObject;
class Model;
class ModelObject;
class GCodePreviewData;
class DynamicPrintConfig;
class ExtrusionPath;
class ExtrusionMultiPath;
class ExtrusionLoop;
class ExtrusionEntity;
class ExtrusionEntityCollection;

// A container for interleaved arrays of 3D vertices and normals,
// possibly indexed by triangles and / or quads.
class GLIndexedVertexArray {
public:
    GLIndexedVertexArray() : 
        vertices_and_normals_interleaved_VBO_id(0),
        triangle_indices_VBO_id(0),
        quad_indices_VBO_id(0)
        { this->setup_sizes(); }
    GLIndexedVertexArray(const GLIndexedVertexArray &rhs) :
        vertices_and_normals_interleaved(rhs.vertices_and_normals_interleaved),
        triangle_indices(rhs.triangle_indices),
        quad_indices(rhs.quad_indices),
        vertices_and_normals_interleaved_VBO_id(0),
        triangle_indices_VBO_id(0),
        quad_indices_VBO_id(0)
        { this->setup_sizes(); }
    GLIndexedVertexArray(GLIndexedVertexArray &&rhs) :
        vertices_and_normals_interleaved(std::move(rhs.vertices_and_normals_interleaved)),
        triangle_indices(std::move(rhs.triangle_indices)),
        quad_indices(std::move(rhs.quad_indices)),
        vertices_and_normals_interleaved_VBO_id(0),
        triangle_indices_VBO_id(0),
        quad_indices_VBO_id(0)
        { this->setup_sizes(); }

    GLIndexedVertexArray& operator=(const GLIndexedVertexArray &rhs)
    {
        assert(vertices_and_normals_interleaved_VBO_id == 0);
        assert(triangle_indices_VBO_id == 0);
        assert(triangle_indices_VBO_id == 0);
        this->vertices_and_normals_interleaved = rhs.vertices_and_normals_interleaved;
        this->triangle_indices                 = rhs.triangle_indices;
        this->quad_indices                     = rhs.quad_indices;
        this->setup_sizes();
        return *this;
    }

    GLIndexedVertexArray& operator=(GLIndexedVertexArray &&rhs) 
    {
        assert(vertices_and_normals_interleaved_VBO_id == 0);
        assert(triangle_indices_VBO_id == 0);
        assert(triangle_indices_VBO_id == 0);
        this->vertices_and_normals_interleaved = std::move(rhs.vertices_and_normals_interleaved);
        this->triangle_indices                 = std::move(rhs.triangle_indices);
        this->quad_indices                     = std::move(rhs.quad_indices);
        this->setup_sizes();
        return *this;
    }

    // Vertices and their normals, interleaved to be used by void glInterleavedArrays(GL_N3F_V3F, 0, x)
    std::vector<float> vertices_and_normals_interleaved;
    std::vector<int>   triangle_indices;
    std::vector<int>   quad_indices;

    // When the geometry data is loaded into the graphics card as Vertex Buffer Objects,
    // the above mentioned std::vectors are cleared and the following variables keep their original length.
    size_t             vertices_and_normals_interleaved_size;
    size_t             triangle_indices_size;
    size_t             quad_indices_size;

    // IDs of the Vertex Array Objects, into which the geometry has been loaded.
    // Zero if the VBOs are not used.
    unsigned int       vertices_and_normals_interleaved_VBO_id;
    unsigned int       triangle_indices_VBO_id;
    unsigned int       quad_indices_VBO_id;

    void load_mesh_flat_shading(const TriangleMesh &mesh);
    void load_mesh_full_shading(const TriangleMesh &mesh);

    inline bool has_VBOs() const { return vertices_and_normals_interleaved_VBO_id != 0; }

    inline void reserve(size_t sz) {
        this->vertices_and_normals_interleaved.reserve(sz * 6);
        this->triangle_indices.reserve(sz * 3);
        this->quad_indices.reserve(sz * 4);
    }

    inline void push_geometry(float x, float y, float z, float nx, float ny, float nz) {
        if (this->vertices_and_normals_interleaved.size() + 6 > this->vertices_and_normals_interleaved.capacity())
            this->vertices_and_normals_interleaved.reserve(next_highest_power_of_2(this->vertices_and_normals_interleaved.size() + 6));
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

    inline void push_geometry(const Pointf3& p, const Vectorf3& n) {
        push_geometry(p.x, p.y, p.z, n.x, n.y, n.z);
    }

    inline void push_triangle(int idx1, int idx2, int idx3) {
        if (this->triangle_indices.size() + 3 > this->vertices_and_normals_interleaved.capacity())
            this->triangle_indices.reserve(next_highest_power_of_2(this->triangle_indices.size() + 3));
        this->triangle_indices.push_back(idx1);
        this->triangle_indices.push_back(idx2);
        this->triangle_indices.push_back(idx3);
    };

    inline void push_quad(int idx1, int idx2, int idx3, int idx4) {
        if (this->quad_indices.size() + 4 > this->vertices_and_normals_interleaved.capacity())
            this->quad_indices.reserve(next_highest_power_of_2(this->quad_indices.size() + 4));
        this->quad_indices.push_back(idx1);
        this->quad_indices.push_back(idx2);
        this->quad_indices.push_back(idx3);
        this->quad_indices.push_back(idx4);
    };

    // Finalize the initialization of the geometry & indices,
    // upload the geometry and indices to OpenGL VBO objects
    // and shrink the allocated data, possibly relasing it if it has been loaded into the VBOs.
    void finalize_geometry(bool use_VBOs);
    // Release the geometry data, release OpenGL VBOs.
    void release_geometry();
    // Render either using an immediate mode, or the VBOs.
    void render() const;
    void render(const std::pair<size_t, size_t> &tverts_range, const std::pair<size_t, size_t> &qverts_range) const;

    // Is there any geometry data stored?
    bool empty() const { return vertices_and_normals_interleaved_size == 0; }

    // Is this object indexed, or is it just a set of triangles?
    bool indexed() const { return ! this->empty() && this->triangle_indices_size + this->quad_indices_size > 0; }

    void clear() {
        this->vertices_and_normals_interleaved.clear();
        this->triangle_indices.clear();
        this->quad_indices.clear();
        this->setup_sizes();
    }

    // Shrink the internal storage to tighly fit the data stored.
    void shrink_to_fit() { 
        if (! this->has_VBOs())
            this->setup_sizes();
        this->vertices_and_normals_interleaved.shrink_to_fit();
        this->triangle_indices.shrink_to_fit();
        this->quad_indices.shrink_to_fit();
    }

    BoundingBoxf3 bounding_box() const {
        BoundingBoxf3 bbox;
        if (! this->vertices_and_normals_interleaved.empty()) {
            bbox.defined = true;
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

private:
    inline void setup_sizes() {
        vertices_and_normals_interleaved_size = this->vertices_and_normals_interleaved.size();
        triangle_indices_size                 = this->triangle_indices.size();
        quad_indices_size                     = this->quad_indices.size();
    }
};

class LayersTexture
{
public:
    LayersTexture() : width(0), height(0), levels(0), cells(0) {}

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
    struct LayerHeightTextureData
    {
        // ID of the layer height texture
        unsigned int texture_id;
        // ID of the shader used to render with the layer height texture
        unsigned int shader_id;
        // The print object to update when generating the layer height texture
        PrintObject* print_object;

        float        z_cursor_relative;
        float        edit_band_width;

        LayerHeightTextureData() { reset(); }

        void reset()
        {
            texture_id = 0;
            shader_id = 0;
            print_object = nullptr;
            z_cursor_relative = 0.0f;
            edit_band_width = 0.0f;
        }

        bool can_use() const { return (texture_id > 0) && (shader_id > 0) && (print_object != nullptr); }
    };

public:
    static const float SELECTED_COLOR[4];
    static const float HOVER_COLOR[4];
    static const float OUTSIDE_COLOR[4];
    static const float SELECTED_OUTSIDE_COLOR[4];

    GLVolume(float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f);
    GLVolume(const float *rgba) : GLVolume(rgba[0], rgba[1], rgba[2], rgba[3]) {}

private:
    // Offset of the volume to be rendered.
    Pointf3               m_origin;
    // Rotation around Z axis of the volume to be rendered.
    float                 m_angle_z;
    // Scale factor of the volume to be rendered.
    float                 m_scale_factor;
    // World matrix of the volume to be rendered.
    std::vector<float>    m_world_mat;
    // Bounding box of this volume, in unscaled coordinates.
    mutable BoundingBoxf3 m_transformed_bounding_box;
    // Whether or not is needed to recalculate the world matrix.
    mutable bool          m_dirty;

public:

    // Bounding box of this volume, in unscaled coordinates.
    BoundingBoxf3       bounding_box;
    // Color of the triangles / quads held by this volume.
    float               color[4];
    // Color used to render this volume.
    float               render_color[4];
    // An ID containing the object ID, volume ID and instance ID.
    int                 composite_id;
    // An ID for group selection. It may be the same for all meshes of all object instances, or for just a single object instance.
    int                 select_group_id;
    // An ID for group dragging. It may be the same for all meshes of all object instances, or for just a single object instance.
    int                 drag_group_id;
    // An ID containing the extruder ID (used to select color).
    int                 extruder_id;
    // Is this object selected?
    bool                selected;
    // Whether or not this volume is active for rendering
    bool                is_active;
    // Whether or not to use this volume when applying zoom_to_volumes()
    bool                zoom_to_volumes;
    // Wheter or not this volume is enabled for outside print volume detection in shader.
    bool                shader_outside_printer_detection_enabled;
    // Wheter or not this volume is outside print volume.
    bool                is_outside;
    // Boolean: Is mouse over this object?
    bool                hover;
    // Wheter or not this volume has been generated from a modifier
    bool                is_modifier;
    // Wheter or not this volume has been generated from the wipe tower
    bool                is_wipe_tower;
    // Wheter or not this volume has been generated from an extrusion path
    bool                is_extrusion_path;

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

    void set_render_color(float r, float g, float b, float a);
    void set_render_color(const float* rgba, unsigned int size);
    // Sets render color in dependence of current state
    void set_render_color();

    const Pointf3& get_origin() const;
    void set_origin(const Pointf3& origin);
    void set_angle_z(float angle_z);
    void set_scale_factor(float scale_factor);

    int                 object_idx() const { return this->composite_id / 1000000; }
    int                 volume_idx() const { return (this->composite_id / 1000) % 1000; }
    int                 instance_idx() const { return this->composite_id % 1000; }

    const std::vector<float>& world_matrix() const;
    BoundingBoxf3       transformed_bounding_box() const;

    bool                empty() const { return this->indexed_vertex_array.empty(); }
    bool                indexed() const { return this->indexed_vertex_array.indexed(); }

    void                set_range(coordf_t low, coordf_t high);
    void                render() const;
    void                render_using_layer_height() const;
    void                render_VBOs(int color_id, int detection_id, int worldmatrix_id) const;
    void                render_legacy() const;

    void                finalize_geometry(bool use_VBOs) { this->indexed_vertex_array.finalize_geometry(use_VBOs); }
    void                release_geometry() { this->indexed_vertex_array.release_geometry(); }

    /************************************************ Layer height texture ****************************************************/
    std::shared_ptr<LayersTexture>  layer_height_texture;
    // Data to render this volume using the layer height texture
    LayerHeightTextureData layer_height_texture_data;

    bool                has_layer_height_texture() const 
        { return this->layer_height_texture.get() != nullptr; }
    size_t              layer_height_texture_width() const 
        { return (this->layer_height_texture.get() == nullptr) ? 0 : this->layer_height_texture->width; }
    size_t              layer_height_texture_height() const 
        { return (this->layer_height_texture.get() == nullptr) ? 0 : this->layer_height_texture->height; }
    size_t              layer_height_texture_cells() const 
        { return (this->layer_height_texture.get() == nullptr) ? 0 : this->layer_height_texture->cells; }
    void*               layer_height_texture_data_ptr_level0() const {
        return (layer_height_texture.get() == nullptr) ? 0 :
            (void*)layer_height_texture->data.data();
    }
    void*               layer_height_texture_data_ptr_level1() const {
        return (layer_height_texture.get() == nullptr) ? 0 :
            (void*)(layer_height_texture->data.data() + layer_height_texture->width * layer_height_texture->height * 4);
    }
    double              layer_height_texture_z_to_row_id() const;
    void                generate_layer_height_texture(PrintObject *print_object, bool force);

    void set_layer_height_texture_data(unsigned int texture_id, unsigned int shader_id, PrintObject* print_object, float z_cursor_relative, float edit_band_width)
    {
        layer_height_texture_data.texture_id = texture_id;
        layer_height_texture_data.shader_id = shader_id;
        layer_height_texture_data.print_object = print_object;
        layer_height_texture_data.z_cursor_relative = z_cursor_relative;
        layer_height_texture_data.edit_band_width = edit_band_width;
    }

    void reset_layer_height_texture_data() { layer_height_texture_data.reset(); }
};

class GLVolumeCollection
{
    // min and max vertex of the print box volume
    float print_box_min[3];
    float print_box_max[3];

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
        const std::string       &drag_by,
        bool                     use_VBOs);

    int load_wipe_tower_preview(
        int obj_idx, float pos_x, float pos_y, float width, float depth, float height, float rotation_angle, bool use_VBOs, bool size_unknown, float brim_width);

    // Render the volumes by OpenGL.
    void render_VBOs() const;
    void render_legacy() const;

    // Finalize the initialization of the geometry & indices,
    // upload the geometry and indices to OpenGL VBO objects
    // and shrink the allocated data, possibly relasing it if it has been loaded into the VBOs.
    void finalize_geometry(bool use_VBOs) { for (auto *v : volumes) v->finalize_geometry(use_VBOs); }
    // Release the geometry data assigned to the volumes.
    // If OpenGL VBOs were allocated, an OpenGL context has to be active to release them.
    void release_geometry() { for (auto *v : volumes) v->release_geometry(); }
    // Clear the geometry
    void clear() { for (auto *v : volumes) delete v; volumes.clear(); }

    bool empty() const { return volumes.empty(); }
    void set_range(double low, double high) { for (GLVolume *vol : this->volumes) vol->set_range(low, high); }

    void set_print_box(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z) {
        print_box_min[0] = min_x; print_box_min[1] = min_y; print_box_min[2] = min_z;
        print_box_max[0] = max_x; print_box_max[1] = max_y; print_box_max[2] = max_z;
    }

    // returns true if all the volumes are completely contained in the print volume
    // returns the containment state in the given out_state, if non-null
    bool check_outside_state(const DynamicPrintConfig* config, ModelInstance::EPrintVolumeState* out_state);
    void reset_outside_state();

    void update_colors_by_extruder(const DynamicPrintConfig* config);

    // Returns a vector containing the sorted list of all the print_zs of the volumes contained in this collection
    std::vector<double> get_current_print_zs(bool active_only) const;

private:
    GLVolumeCollection(const GLVolumeCollection &other);
    GLVolumeCollection& operator=(const GLVolumeCollection &);
};

class _3DScene
{
    static GUI::GLCanvas3DManager s_canvas_mgr;

public:
    static void init_gl();
    static std::string get_gl_info(bool format_as_html, bool extensions);
    static bool use_VBOs();

    static bool add_canvas(wxGLCanvas* canvas);
    static bool remove_canvas(wxGLCanvas* canvas);
    static void remove_all_canvases();

    static bool init(wxGLCanvas* canvas);

    static void set_as_dirty(wxGLCanvas* canvas);

    static unsigned int get_volumes_count(wxGLCanvas* canvas);
    static void reset_volumes(wxGLCanvas* canvas);
    static void deselect_volumes(wxGLCanvas* canvas);
    static void select_volume(wxGLCanvas* canvas, unsigned int id);
    static void update_volumes_selection(wxGLCanvas* canvas, const std::vector<int>& selections);
    static int check_volumes_outside_state(wxGLCanvas* canvas, const DynamicPrintConfig* config);
    static bool move_volume_up(wxGLCanvas* canvas, unsigned int id);
    static bool move_volume_down(wxGLCanvas* canvas, unsigned int id);

    static void set_objects_selections(wxGLCanvas* canvas, const std::vector<int>& selections);

    static void set_config(wxGLCanvas* canvas, DynamicPrintConfig* config);
    static void set_print(wxGLCanvas* canvas, Print* print);
    static void set_model(wxGLCanvas* canvas, Model* model);

    static void set_bed_shape(wxGLCanvas* canvas, const Pointfs& shape);
    static void set_auto_bed_shape(wxGLCanvas* canvas);

    static BoundingBoxf3 get_volumes_bounding_box(wxGLCanvas* canvas);

    static void set_axes_length(wxGLCanvas* canvas, float length);

    static void set_cutting_plane(wxGLCanvas* canvas, float z, const ExPolygons& polygons);

    static void set_color_by(wxGLCanvas* canvas, const std::string& value);
    static void set_select_by(wxGLCanvas* canvas, const std::string& value);
    static void set_drag_by(wxGLCanvas* canvas, const std::string& value);

    static bool is_layers_editing_enabled(wxGLCanvas* canvas);
    static bool is_layers_editing_allowed(wxGLCanvas* canvas);
    static bool is_shader_enabled(wxGLCanvas* canvas);

    static bool is_reload_delayed(wxGLCanvas* canvas);

    static void enable_layers_editing(wxGLCanvas* canvas, bool enable);
    static void enable_warning_texture(wxGLCanvas* canvas, bool enable);
    static void enable_legend_texture(wxGLCanvas* canvas, bool enable);
    static void enable_picking(wxGLCanvas* canvas, bool enable);
    static void enable_moving(wxGLCanvas* canvas, bool enable);
    static void enable_gizmos(wxGLCanvas* canvas, bool enable);
    static void enable_shader(wxGLCanvas* canvas, bool enable);
    static void enable_force_zoom_to_bed(wxGLCanvas* canvas, bool enable);
    static void enable_dynamic_background(wxGLCanvas* canvas, bool enable);
    static void allow_multisample(wxGLCanvas* canvas, bool allow);

    static void zoom_to_bed(wxGLCanvas* canvas);
    static void zoom_to_volumes(wxGLCanvas* canvas);
    static void select_view(wxGLCanvas* canvas, const std::string& direction);
    static void set_viewport_from_scene(wxGLCanvas* canvas, wxGLCanvas* other);

    static void update_volumes_colors_by_extruder(wxGLCanvas* canvas);
    static void update_gizmos_data(wxGLCanvas* canvas);

    static void render(wxGLCanvas* canvas);

    static std::vector<double> get_current_print_zs(wxGLCanvas* canvas, bool active_only);
    static void set_toolpaths_range(wxGLCanvas* canvas, double low, double high);

    static void register_on_viewport_changed_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_double_click_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_right_click_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_select_object_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_model_update_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_remove_object_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_arrange_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_rotate_object_left_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_rotate_object_right_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_scale_object_uniformly_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_increase_objects_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_decrease_objects_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_instance_moved_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_wipe_tower_moved_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_enable_action_buttons_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_gizmo_scale_uniformly_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_gizmo_rotate_callback(wxGLCanvas* canvas, void* callback);
    static void register_on_update_geometry_info_callback(wxGLCanvas* canvas, void* callback);

    static std::vector<int> load_object(wxGLCanvas* canvas, const ModelObject* model_object, int obj_idx, std::vector<int> instance_idxs);
    static std::vector<int> load_object(wxGLCanvas* canvas, const Model* model, int obj_idx);

    static void reload_scene(wxGLCanvas* canvas, bool force);

    static void load_gcode_preview(wxGLCanvas* canvas, const GCodePreviewData* preview_data, const std::vector<std::string>& str_tool_colors);
    static void load_preview(wxGLCanvas* canvas, const std::vector<std::string>& str_tool_colors);

    static void reset_legend_texture();

    static void thick_lines_to_verts(const Lines& lines, const std::vector<double>& widths, const std::vector<double>& heights, bool closed, double top_z, GLVolume& volume);
    static void thick_lines_to_verts(const Lines3& lines, const std::vector<double>& widths, const std::vector<double>& heights, bool closed, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionPath& extrusion_path, float print_z, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionPath& extrusion_path, float print_z, const Point& copy, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionLoop& extrusion_loop, float print_z, const Point& copy, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionMultiPath& extrusion_multi_path, float print_z, const Point& copy, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionEntityCollection& extrusion_entity_collection, float print_z, const Point& copy, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionEntity* extrusion_entity, float print_z, const Point& copy, GLVolume& volume);
    static void polyline3_to_verts(const Polyline3& polyline, double width, double height, GLVolume& volume);
    static void point3_to_verts(const Point3& point, double width, double height, GLVolume& volume);
};

}

#endif
