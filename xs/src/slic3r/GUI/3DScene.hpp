#ifndef slic3r_3DScene_hpp_
#define slic3r_3DScene_hpp_

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/Point.hpp"
#include "../../libslic3r/Line.hpp"
#include "../../libslic3r/TriangleMesh.hpp"
#include "../../libslic3r/Utils.hpp"

class wxBitmap;

namespace Slic3r {

class Print;
class PrintObject;
class Model;
class ModelObject;
class GCodePreviewData;
class DynamicPrintConfig;

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

        bool can_use() { return (texture_id > 0) && (shader_id > 0) && (print_object != nullptr); }
    };

public:
    static const float SELECTED_COLOR[4];
    static const float HOVER_COLOR[4];
    static const float OUTSIDE_COLOR[4];
    static const float SELECTED_OUTSIDE_COLOR[4];

    GLVolume(float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f) :
        composite_id(-1),
        select_group_id(-1),
        drag_group_id(-1),
        extruder_id(0),
        selected(false),
        is_active(true),
        zoom_to_volumes(true),
        outside_printer_detection_enabled(true),
        is_outside(false),
        hover(false),
        is_modifier(false),
        is_wipe_tower(false),
        tverts_range(0, size_t(-1)),
        qverts_range(0, size_t(-1))
    {
        color[0] = r;
        color[1] = g;
        color[2] = b;
        color[3] = a;
        set_render_color(r, g, b, a);
    }
    GLVolume(const float *rgba) : GLVolume(rgba[0], rgba[1], rgba[2], rgba[3]) {}

    std::vector<int> load_object(
        const ModelObject        *model_object, 
        const std::vector<int>   &instance_idxs,
        const std::string        &color_by,
        const std::string        &select_by,
        const std::string        &drag_by);

    int load_wipe_tower_preview(
        int obj_idx, float pos_x, float pos_y, float width, float depth, float height, float rotation_angle, bool use_VBOs);

    // Bounding box of this volume, in unscaled coordinates.
    BoundingBoxf3       bounding_box;
    // Offset of the volume to be rendered.
    Pointf3             origin;
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
    // Wheter or not this volume is enabled for outside print volume detection.
    bool                outside_printer_detection_enabled;
    // Wheter or not this volume is outside print volume.
    bool                is_outside;
    // Boolean: Is mouse over this object?
    bool                hover;
    // Wheter or not this volume has been generated from a modifier
    bool                 is_modifier;
    // Wheter or not this volume has been generated from the wipe tower
    bool                 is_wipe_tower;

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

    int                 object_idx() const { return this->composite_id / 1000000; }
    int                 volume_idx() const { return (this->composite_id / 1000) % 1000; }
    int                 instance_idx() const { return this->composite_id % 1000; }
    BoundingBoxf3       transformed_bounding_box() const { BoundingBoxf3 bb = this->bounding_box; bb.translate(this->origin); return bb; }

    bool                empty() const { return this->indexed_vertex_array.empty(); }
    bool                indexed() const { return this->indexed_vertex_array.indexed(); }

    void                set_range(coordf_t low, coordf_t high);
    void                render() const;
    void                render_using_layer_height() const;
    void                finalize_geometry(bool use_VBOs) { this->indexed_vertex_array.finalize_geometry(use_VBOs); }
    void                release_geometry() { this->indexed_vertex_array.release_geometry(); }

    /************************************************ Layer height texture ****************************************************/
    std::shared_ptr<GLTexture>  layer_height_texture;
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
        int obj_idx, float pos_x, float pos_y, float width, float depth, float height, float rotation_angle, bool use_VBOs);

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

    void update_outside_state(const DynamicPrintConfig* config, bool all_inside);
    void update_colors_by_extruder(const DynamicPrintConfig* config);

    // Returns a vector containing the sorted list of all the print_zs of the volumes contained in this collection
    std::vector<double> get_current_print_zs() const;

private:
    GLVolumeCollection(const GLVolumeCollection &other);
    GLVolumeCollection& operator=(const GLVolumeCollection &);
};

class _3DScene
{
    struct GCodePreviewVolumeIndex
    {
        enum EType
        {
            Extrusion,
            Travel,
            Retraction,
            Unretraction,
            Shell,
            Num_Geometry_Types
        };

        struct FirstVolume
        {
            EType type;
            unsigned int flag;
            // Index of the first volume in a GLVolumeCollection.
            unsigned int id;

            FirstVolume(EType type, unsigned int flag, unsigned int id) : type(type), flag(flag), id(id) {}
        };

        std::vector<FirstVolume> first_volumes;

        void reset() { first_volumes.clear(); }
    };

    static GCodePreviewVolumeIndex s_gcode_preview_volume_index;

    class TextureBase
    {
    protected:
        unsigned int m_tex_id;
        unsigned int m_tex_width;
        unsigned int m_tex_height;

        // generate() fills in m_data with the pixels, while finalize() moves the data to the GPU before rendering.
        std::vector<unsigned char> m_data;

    public:
        TextureBase() : m_tex_id(0), m_tex_width(0), m_tex_height(0) {}
        virtual ~TextureBase() { _destroy_texture(); }

        // If not loaded, load the texture data into the GPU. Return a texture ID or 0 if the texture has zero size.
        unsigned int finalize();

        unsigned int get_texture_id() const { return m_tex_id; }
        unsigned int get_texture_width() const { return m_tex_width; }
        unsigned int get_texture_height() const { return m_tex_height; }

        void reset_texture() { _destroy_texture(); }

    private:
        void _destroy_texture();
    };

    class WarningTexture : public TextureBase
    {
        static const unsigned char Background_Color[3];
        static const unsigned char Opacity;

    public:
        WarningTexture() : TextureBase() {}

        // Generate a texture data, but don't load it into the GPU yet, as the glcontext may not be valid yet.
        bool generate(const std::string& msg);
    };

    class LegendTexture : public TextureBase
    {
        static const unsigned int Px_Title_Offset = 5;
        static const unsigned int Px_Text_Offset = 5;
        static const unsigned int Px_Square = 20;
        static const unsigned int Px_Square_Contour = 1;
        static const unsigned int Px_Border = Px_Square / 2;
        static const unsigned char Squares_Border_Color[3];
        static const unsigned char Background_Color[3];
        static const unsigned char Opacity;

    public:
        LegendTexture() : TextureBase() {}

        // Generate a texture data, but don't load it into the GPU yet, as the glcontext may not be valid yet.
        bool generate(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors);
    };

    static LegendTexture s_legend_texture;
    static WarningTexture s_warning_texture;

public:
    static void _glew_init();

    static void load_gcode_preview(const Print* print, const GCodePreviewData* preview_data, GLVolumeCollection* volumes, const std::vector<std::string>& str_tool_colors, bool use_VBOs);

    static unsigned int get_legend_texture_width();
    static unsigned int get_legend_texture_height();

    static void reset_legend_texture();
    static unsigned int finalize_legend_texture();

    static unsigned int get_warning_texture_width();
    static unsigned int get_warning_texture_height();

    // generates a warning texture containing the given message
    static void generate_warning_texture(const std::string& msg);
    static void reset_warning_texture();
    static unsigned int finalize_warning_texture();

    static void _load_print_toolpaths(
        const Print                     *print,
        GLVolumeCollection              *volumes,
        const std::vector<std::string>  &tool_colors,
        bool                             use_VBOs);

    static void _load_print_object_toolpaths(
        const PrintObject               *print_object,
        GLVolumeCollection              *volumes,
        const std::vector<std::string>  &tool_colors,
        bool                             use_VBOs);

    static void _load_wipe_tower_toolpaths(
        const Print                    *print,
        GLVolumeCollection             *volumes,
        const std::vector<std::string> &tool_colors_str,
        bool                            use_VBOs);

private:
    // generates gcode extrusion paths geometry
    static void _load_gcode_extrusion_paths(const GCodePreviewData& preview_data, GLVolumeCollection& volumes, const std::vector<float>& tool_colors, bool use_VBOs);
    // generates gcode travel paths geometry
    static void _load_gcode_travel_paths(const GCodePreviewData& preview_data, GLVolumeCollection& volumes, const std::vector<float>& tool_colors, bool use_VBOs);
    static bool _travel_paths_by_type(const GCodePreviewData& preview_data, GLVolumeCollection& volumes);
    static bool _travel_paths_by_feedrate(const GCodePreviewData& preview_data, GLVolumeCollection& volumes);
    static bool _travel_paths_by_tool(const GCodePreviewData& preview_data, GLVolumeCollection& volumes, const std::vector<float>& tool_colors);
    // generates gcode retractions geometry
    static void _load_gcode_retractions(const GCodePreviewData& preview_data, GLVolumeCollection& volumes, bool use_VBOs);
    // generates gcode unretractions geometry
    static void _load_gcode_unretractions(const GCodePreviewData& preview_data, GLVolumeCollection& volumes, bool use_VBOs);
    // sets gcode geometry visibility according to user selection
    static void _update_gcode_volumes_visibility(const GCodePreviewData& preview_data, GLVolumeCollection& volumes);
    // generates the legend texture in dependence of the current shown view type
    static void _generate_legend_texture(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors);
    // generates objects and wipe tower geometry
    static void _load_shells(const Print& print, GLVolumeCollection& volumes, bool use_VBOs);
};

}

#endif
