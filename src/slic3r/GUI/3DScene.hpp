#ifndef slic3r_3DScene_hpp_
#define slic3r_3DScene_hpp_

#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GLCanvas3DManager.hpp"

#include <functional>

#ifndef NDEBUG
#define HAS_GLSAFE
#endif

#ifdef HAS_GLSAFE
extern void glAssertRecentCallImpl();
inline void glAssertRecentCall() { glAssertRecentCallImpl(); }
#define glsafe(cmd) do { cmd; glAssertRecentCallImpl(); } while (false)
#else
inline void glAssertRecentCall() { }
#define glsafe(cmd) cmd
#endif

namespace Slic3r {
namespace GUI {
class Bed3D;
struct Camera;
class GLToolbar;
} // namespace GUI

class Print;
class PrintObject;
class SLAPrint;
class SLAPrintObject;
enum  SLAPrintObjectStep : unsigned int;
class Model;
class ModelObject;
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

    inline void push_geometry(const Vec3d& p, const Vec3d& n) {
        push_geometry(p(0), p(1), p(2), n(0), n(1), n(2));
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
            bbox.min(0) = bbox.max(0) = this->vertices_and_normals_interleaved[3];
            bbox.min(1) = bbox.max(1) = this->vertices_and_normals_interleaved[4];
            bbox.min(2) = bbox.max(2) = this->vertices_and_normals_interleaved[5];
            for (size_t i = 9; i < this->vertices_and_normals_interleaved.size(); i += 6) {
                const float *verts = this->vertices_and_normals_interleaved.data() + i;
                bbox.min(0) = std::min<coordf_t>(bbox.min(0), verts[0]);
                bbox.min(1) = std::min<coordf_t>(bbox.min(1), verts[1]);
                bbox.min(2) = std::min<coordf_t>(bbox.min(2), verts[2]);
                bbox.max(0) = std::max<coordf_t>(bbox.max(0), verts[0]);
                bbox.max(1) = std::max<coordf_t>(bbox.max(1), verts[1]);
                bbox.max(2) = std::max<coordf_t>(bbox.max(2), verts[2]);
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

class GLVolume {
public:
    static const float SELECTED_COLOR[4];
    static const float HOVER_COLOR[4];
    static const float OUTSIDE_COLOR[4];
    static const float SELECTED_OUTSIDE_COLOR[4];
    static const float DISABLED_COLOR[4];
    static const float SLA_SUPPORT_COLOR[4];
    static const float SLA_PAD_COLOR[4];

    GLVolume(float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f);
    GLVolume(const float *rgba) : GLVolume(rgba[0], rgba[1], rgba[2], rgba[3]) {}
    ~GLVolume();

private:
    Geometry::Transformation m_instance_transformation;
    Geometry::Transformation m_volume_transformation;

    // Shift in z required by sla supports+pad
    double                m_sla_shift_z;
    // Bounding box of this volume, in unscaled coordinates.
    mutable BoundingBoxf3 m_transformed_bounding_box;
    // Whether or not is needed to recalculate the transformed bounding box.
    mutable bool          m_transformed_bounding_box_dirty;
    // Pointer to convex hull of the original mesh, if any.
    // This object may or may not own the convex hull instance based on m_convex_hull_owned
    const TriangleMesh*   m_convex_hull;
    bool                  m_convex_hull_owned;
    // Bounding box of this volume, in unscaled coordinates.
    mutable BoundingBoxf3 m_transformed_convex_hull_bounding_box;
    // Whether or not is needed to recalculate the transformed convex hull bounding box.
    mutable bool          m_transformed_convex_hull_bounding_box_dirty;

public:
    // Bounding box of this volume, in unscaled coordinates.
    BoundingBoxf3       bounding_box;
    // Color of the triangles / quads held by this volume.
    float               color[4];
    // Color used to render this volume.
    float               render_color[4];
    struct CompositeID {
        CompositeID(int object_id, int volume_id, int instance_id) : object_id(object_id), volume_id(volume_id), instance_id(instance_id) {}
        CompositeID() : object_id(-1), volume_id(-1), instance_id(-1) {}
        // Object ID, which is equal to the index of the respective ModelObject in Model.objects array.
        int             object_id;
        // Volume ID, which is equal to the index of the respective ModelVolume in ModelObject.volumes array.
        // If negative, it is an index of a geometry produced by the PrintObject for the respective ModelObject,
        // and which has no associated ModelVolume in ModelObject.volumes. For example, SLA supports.
        // Volume with a negative volume_id cannot be picked independently, it will pick the associated instance.
        int             volume_id;
        // Instance ID, which is equal to the index of the respective ModelInstance in ModelObject.instances array.
        int             instance_id;
    };
    CompositeID         composite_id;
    // Fingerprint of the source geometry. For ModelVolumes, it is the ModelVolume::ID and ModelInstanceID, 
    // for generated volumes it is the timestamp generated by PrintState::invalidate() or PrintState::set_done(),
    // and the associated ModelInstanceID.
    // Valid geometry_id should always be positive.
    std::pair<size_t, size_t> geometry_id;
    // An ID containing the extruder ID (used to select color).
    int                 extruder_id;
    // Is this object selected?
    bool                selected;
    // Is this object disabled from selection?
    bool                disabled;
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
    // Wheter or not to always render this volume using its own alpha 
    bool                force_transparent;

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
    // set color according to model volume
    void set_color_from_model_volume(const ModelVolume *model_volume);

    const Geometry::Transformation& get_instance_transformation() const { return m_instance_transformation; }
    void set_instance_transformation(const Geometry::Transformation& transformation) { m_instance_transformation = transformation; set_bounding_boxes_as_dirty(); }

    const Vec3d& get_instance_offset() const { return m_instance_transformation.get_offset(); }
    double get_instance_offset(Axis axis) const { return m_instance_transformation.get_offset(axis); }

    void set_instance_offset(const Vec3d& offset) { m_instance_transformation.set_offset(offset); set_bounding_boxes_as_dirty(); }
    void set_instance_offset(Axis axis, double offset) { m_instance_transformation.set_offset(axis, offset); set_bounding_boxes_as_dirty(); }

    const Vec3d& get_instance_rotation() const { return m_instance_transformation.get_rotation(); }
    double get_instance_rotation(Axis axis) const { return m_instance_transformation.get_rotation(axis); }

    void set_instance_rotation(const Vec3d& rotation) { m_instance_transformation.set_rotation(rotation); set_bounding_boxes_as_dirty(); }
    void set_instance_rotation(Axis axis, double rotation) { m_instance_transformation.set_rotation(axis, rotation); set_bounding_boxes_as_dirty(); }

    Vec3d get_instance_scaling_factor() const { return m_instance_transformation.get_scaling_factor(); }
    double get_instance_scaling_factor(Axis axis) const { return m_instance_transformation.get_scaling_factor(axis); }

    void set_instance_scaling_factor(const Vec3d& scaling_factor) { m_instance_transformation.set_scaling_factor(scaling_factor); set_bounding_boxes_as_dirty(); }
    void set_instance_scaling_factor(Axis axis, double scaling_factor) { m_instance_transformation.set_scaling_factor(axis, scaling_factor); set_bounding_boxes_as_dirty(); }

    const Vec3d& get_instance_mirror() const { return m_instance_transformation.get_mirror(); }
    double get_instance_mirror(Axis axis) const { return m_instance_transformation.get_mirror(axis); }

    void set_instance_mirror(const Vec3d& mirror) { m_instance_transformation.set_mirror(mirror); set_bounding_boxes_as_dirty(); }
    void set_instance_mirror(Axis axis, double mirror) { m_instance_transformation.set_mirror(axis, mirror); set_bounding_boxes_as_dirty(); }

    const Geometry::Transformation& get_volume_transformation() const { return m_volume_transformation; }
    void set_volume_transformation(const Geometry::Transformation& transformation) { m_volume_transformation = transformation; set_bounding_boxes_as_dirty(); }

    const Vec3d& get_volume_offset() const { return m_volume_transformation.get_offset(); }
    double get_volume_offset(Axis axis) const { return m_volume_transformation.get_offset(axis); }

    void set_volume_offset(const Vec3d& offset) { m_volume_transformation.set_offset(offset); set_bounding_boxes_as_dirty(); }
    void set_volume_offset(Axis axis, double offset) { m_volume_transformation.set_offset(axis, offset); set_bounding_boxes_as_dirty(); }

    const Vec3d& get_volume_rotation() const { return m_volume_transformation.get_rotation(); }
    double get_volume_rotation(Axis axis) const { return m_volume_transformation.get_rotation(axis); }

    void set_volume_rotation(const Vec3d& rotation) { m_volume_transformation.set_rotation(rotation); set_bounding_boxes_as_dirty(); }
    void set_volume_rotation(Axis axis, double rotation) { m_volume_transformation.set_rotation(axis, rotation); set_bounding_boxes_as_dirty(); }

    Vec3d get_volume_scaling_factor() const { return m_volume_transformation.get_scaling_factor(); }
    double get_volume_scaling_factor(Axis axis) const { return m_volume_transformation.get_scaling_factor(axis); }

    void set_volume_scaling_factor(const Vec3d& scaling_factor) { m_volume_transformation.set_scaling_factor(scaling_factor); set_bounding_boxes_as_dirty(); }
    void set_volume_scaling_factor(Axis axis, double scaling_factor) { m_volume_transformation.set_scaling_factor(axis, scaling_factor); set_bounding_boxes_as_dirty(); }

    const Vec3d& get_volume_mirror() const { return m_volume_transformation.get_mirror(); }
    double get_volume_mirror(Axis axis) const { return m_volume_transformation.get_mirror(axis); }

    void set_volume_mirror(const Vec3d& mirror) { m_volume_transformation.set_mirror(mirror); set_bounding_boxes_as_dirty(); }
    void set_volume_mirror(Axis axis, double mirror) { m_volume_transformation.set_mirror(axis, mirror); set_bounding_boxes_as_dirty(); }
     
    double get_sla_shift_z() const { return m_sla_shift_z; }
    void set_sla_shift_z(double z) { m_sla_shift_z = z; }

    void set_convex_hull(const TriangleMesh *convex_hull, bool owned);

    int                 object_idx() const { return this->composite_id.object_id; }
    int                 volume_idx() const { return this->composite_id.volume_id; }
    int                 instance_idx() const { return this->composite_id.instance_id; }

    Transform3d         world_matrix() const;

    const BoundingBoxf3& transformed_bounding_box() const;
    const BoundingBoxf3& transformed_convex_hull_bounding_box() const;

    bool                empty() const { return this->indexed_vertex_array.empty(); }
    bool                indexed() const { return this->indexed_vertex_array.indexed(); }

    void                set_range(coordf_t low, coordf_t high);
    void                render() const;
    void                render_VBOs(int color_id, int detection_id, int worldmatrix_id) const;
    void                render_legacy() const;

    void                finalize_geometry(bool use_VBOs) { this->indexed_vertex_array.finalize_geometry(use_VBOs); }
    void                release_geometry() { this->indexed_vertex_array.release_geometry(); }

    void                set_bounding_boxes_as_dirty() { m_transformed_bounding_box_dirty = true; m_transformed_convex_hull_bounding_box_dirty = true; }
};

typedef std::vector<GLVolume*> GLVolumePtrs;

class GLVolumeCollection
{
public:
    enum ERenderType : unsigned char
    {
        Opaque,
        Transparent,
        All
    };

private:
    // min and max vertex of the print box volume
    float print_box_min[3];
    float print_box_max[3];

    // z range for clipping in shaders
    float z_range[2];

public:
    GLVolumePtrs volumes;

    GLVolumeCollection() {};
    ~GLVolumeCollection() { clear(); };

    std::vector<int> load_object(
        const ModelObject       *model_object,
        int                      obj_idx,
        const std::vector<int>  &instance_idxs,
        const std::string       &color_by,
        bool                     use_VBOs);

    int load_object_volume(
        const ModelObject       *model_object,
        int                      obj_idx,
        int                      volume_idx,
        int                      instance_idx,
        const std::string       &color_by,
        bool                     use_VBOs);

    // Load SLA auxiliary GLVolumes (for support trees or pad).
    void load_object_auxiliary(
        const SLAPrintObject           *print_object,
        int                             obj_idx,
        // pairs of <instance_idx, print_instance_idx>
        const std::vector<std::pair<size_t, size_t>> &instances,
        SLAPrintObjectStep              milestone,
        // Timestamp of the last change of the milestone
        size_t                          timestamp,
        bool                            use_VBOs);

    int load_wipe_tower_preview(
        int obj_idx, float pos_x, float pos_y, float width, float depth, float height, float rotation_angle, bool use_VBOs, bool size_unknown, float brim_width);

    // Render the volumes by OpenGL.
	void render_VBOs(ERenderType type, bool disable_cullface, std::function<bool(const GLVolume&)> filter_func = std::function<bool(const GLVolume&)>()) const;
    void render_legacy(ERenderType type, bool disable_cullface, std::function<bool(const GLVolume&)> filter_func = std::function<bool(const GLVolume&)>()) const;

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

    void set_z_range(float min_z, float max_z) { z_range[0] = min_z; z_range[1] = max_z; }

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

class GLModel
{
protected:
    GLVolume m_volume;
    bool m_useVBOs;
    std::string m_filename;

public:
    GLModel();
    virtual ~GLModel();

    bool init(bool useVBOs) { return on_init(useVBOs); }
    bool init_from_file(const std::string& filename, bool useVBOs) { return on_init_from_file(filename, useVBOs); }

    void center_around(const Vec3d& center) { m_volume.set_volume_offset(center - m_volume.bounding_box.center()); }
    void set_color(const float* color, unsigned int size);

    const Vec3d& get_offset() const;
    void set_offset(const Vec3d& offset);
    const Vec3d& get_rotation() const;
    void set_rotation(const Vec3d& rotation);
    const Vec3d& get_scale() const;
    void set_scale(const Vec3d& scale);

    const std::string& get_filename() const { return m_filename; }
    const BoundingBoxf3& get_bounding_box() const { return m_volume.bounding_box; }

    void reset();

    void render() const; 

protected:
    virtual bool on_init(bool useVBOs) { return false; }
    virtual bool on_init_from_file(const std::string& filename, bool useVBOs) { return false; }

private:
    void render_VBOs() const;
    void render_legacy() const;
};

class GLArrow : public GLModel
{
protected:
    virtual bool on_init(bool useVBOs);
};

class GLCurvedArrow : public GLModel
{
    unsigned int m_resolution;

public:
    explicit GLCurvedArrow(unsigned int resolution);

protected:
    virtual bool on_init(bool useVBOs);
};

class GLBed : public GLModel
{
protected:
    virtual bool on_init_from_file(const std::string& filename, bool useVBOs);
};

class _3DScene
{
    static GUI::GLCanvas3DManager s_canvas_mgr;

public:
    static std::string get_gl_info(bool format_as_html, bool extensions);

    static bool add_canvas(wxGLCanvas* canvas, GUI::Bed3D& bed, GUI::Camera& camera, GUI::GLToolbar& view_toolbar);
    static bool remove_canvas(wxGLCanvas* canvas);
    static void remove_all_canvases();

    static bool init(wxGLCanvas* canvas);

    static GUI::GLCanvas3D* get_canvas(wxGLCanvas* canvas);

    static void thick_lines_to_verts(const Lines& lines, const std::vector<double>& widths, const std::vector<double>& heights, bool closed, double top_z, GLVolume& volume);
    static void thick_lines_to_verts(const Lines3& lines, const std::vector<double>& widths, const std::vector<double>& heights, bool closed, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionPath& extrusion_path, float print_z, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionPath& extrusion_path, float print_z, const Point& copy, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionLoop& extrusion_loop, float print_z, const Point& copy, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionMultiPath& extrusion_multi_path, float print_z, const Point& copy, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionEntityCollection& extrusion_entity_collection, float print_z, const Point& copy, GLVolume& volume);
    static void extrusionentity_to_verts(const ExtrusionEntity* extrusion_entity, float print_z, const Point& copy, GLVolume& volume);
    static void polyline3_to_verts(const Polyline3& polyline, double width, double height, GLVolume& volume);
    static void point3_to_verts(const Vec3crd& point, double width, double height, GLVolume& volume);
};

}

#endif
