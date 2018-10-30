#ifndef slic3r_Model_hpp_
#define slic3r_Model_hpp_

#include "libslic3r.h"
#include "PrintConfig.hpp"
#include "Layer.hpp"
#include "Point.hpp"
#include "TriangleMesh.hpp"
#include "Slicing.hpp"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

class Model;
class ModelInstance;
class ModelMaterial;
class ModelObject;
class ModelVolume;
class PresetBundle;
class Print;

typedef std::string t_model_material_id;
typedef std::string t_model_material_attribute;
typedef std::map<t_model_material_attribute,std::string> t_model_material_attributes;

typedef std::map<t_model_material_id,ModelMaterial*> ModelMaterialMap;
typedef std::vector<ModelObject*> ModelObjectPtrs;
typedef std::vector<ModelVolume*> ModelVolumePtrs;
typedef std::vector<ModelInstance*> ModelInstancePtrs;

// Unique identifier of a Model, ModelObject, ModelVolume, ModelInstance or ModelMaterial.
// Used to synchronize the front end (UI) with the back end (BackgroundSlicingProcess / Print / PrintObject)
typedef size_t ModelID;

// Base for Model, ModelObject, ModelVolume, ModelInstance or ModelMaterial to provide a unique ID
// to synchronize the front end (UI) with the back end (BackgroundSlicingProcess / Print / PrintObject).
// Achtung! The s_last_id counter is not thread safe, so it is expected, that the ModelBase derived instances
// are only instantiated from the main thread.
class ModelBase
{
public:
    ModelID  id() const { return m_id; }

protected:
    // Constructor to be only called by derived classes.
    ModelBase() {}
    ModelID  m_id = generate_new_id();

private:
    static inline ModelID generate_new_id() { return s_last_id ++; }
    static ModelID s_last_id;
};

// Material, which may be shared across multiple ModelObjects of a single Model.
class ModelMaterial : public ModelBase
{
    friend class Model;
public:
    // Attributes are defined by the AMF file format, but they don't seem to be used by Slic3r for any purpose.
    t_model_material_attributes attributes;
    // Dynamic configuration storage for the object specific configuration values, overriding the global configuration.
    DynamicPrintConfig config;

    Model* get_model() const { return m_model; }
    void apply(const t_model_material_attributes &attributes)
        { this->attributes.insert(attributes.begin(), attributes.end()); }

private:
    // Parent, owning this material.
    Model *m_model;
    
    ModelMaterial(Model *model) : m_model(model) {}
    ModelMaterial(Model *model, const ModelMaterial &other) : attributes(other.attributes), config(other.config), m_model(model) {}
    explicit ModelMaterial(ModelMaterial &rhs) = delete;
    ModelMaterial& operator=(ModelMaterial &rhs) = delete;
};

// A printable object, possibly having multiple print volumes (each with its own set of parameters and materials),
// and possibly having multiple modifier volumes, each modifier volume with its set of parameters and materials.
// Each ModelObject may be instantiated mutliple times, each instance having different placement on the print bed,
// different rotation and different uniform scaling.
class ModelObject : public ModelBase
{
    friend class Model;
public:
    std::string             name;
    std::string             input_file;    // XXX: consider fs::path
    // Instances of this ModelObject. Each instance defines a shift on the print bed, rotation around the Z axis and a uniform scaling.
    // Instances are owned by this ModelObject.
    ModelInstancePtrs       instances;
    // Printable and modifier volumes, each with its material ID and a set of override parameters.
    // ModelVolumes are owned by this ModelObject.
    ModelVolumePtrs         volumes;
    // Configuration parameters specific to a single ModelObject, overriding the global Slic3r settings.
    DynamicPrintConfig      config;
    // Variation of a layer thickness for spans of Z coordinates.
    t_layer_height_ranges   layer_height_ranges;
    // Profile of increasing z to a layer height, to be linearly interpolated when calculating the layers.
    // The pairs of <z, layer_height> are packed into a 1D array to simplify handling by the Perl XS.
    std::vector<coordf_t>   layer_height_profile;
    // layer_height_profile is initialized when the layer editing mode is entered.
    // Only if the user really modified the layer height, layer_height_profile_valid is set
    // and used subsequently by the PrintObject.
    bool                    layer_height_profile_valid;

    // This vector holds position of selected support points for SLA. The data are
    // saved in mesh coordinates to allow using them for several instances.
    std::vector<Vec3f>      sla_support_points;

    /* This vector accumulates the total translation applied to the object by the
        center_around_origin() method. Callers might want to apply the same translation
        to new volumes before adding them to this object in order to preserve alignment
        when user expects that. */
    Vec3d                   origin_translation;

    // Assign a ModelObject to this object while keeping the original pointer to the parent Model.
	// Make a deep copy.
	ModelObject&            assign(const ModelObject *rhs, bool copy_volumes = true);

    Model*                  get_model() const { return m_model; };
    
    ModelVolume*            add_volume(const TriangleMesh &mesh);
    ModelVolume*            add_volume(TriangleMesh &&mesh);
    ModelVolume*            add_volume(const ModelVolume &volume);
    void                    delete_volume(size_t idx);
    void                    clear_volumes();
    bool                    is_multiparts() const { return volumes.size() > 1; }

    ModelInstance*          add_instance();
    ModelInstance*          add_instance(const ModelInstance &instance);
    ModelInstance*          add_instance(const Vec3d &offset, const Vec3d &scaling_factor, const Vec3d &rotation);
    void                    delete_instance(size_t idx);
    void                    delete_last_instance();
    void                    clear_instances();

    // Returns the bounding box of the transformed instances.
    // This bounding box is approximate and not snug.
    // This bounding box is being cached.
    const BoundingBoxf3& bounding_box() const;
    void invalidate_bounding_box() { m_bounding_box_valid = false; }

    // A mesh containing all transformed instances of this object.
    TriangleMesh mesh() const;
    // Non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
    // Currently used by ModelObject::mesh() and to calculate the 2D envelope for 2D platter.
    TriangleMesh raw_mesh() const;
    // A transformed snug bounding box around the non-modifier object volumes, without the translation applied.
    // This bounding box is only used for the actual slicing.
    BoundingBoxf3 raw_bounding_box() const;
    // A snug bounding box around the transformed non-modifier object volumes.
    BoundingBoxf3 instance_bounding_box(size_t instance_idx, bool dont_translate = false) const;
    void center_around_origin();
    void ensure_on_bed();
    void translate_instances(const Vec3d& vector);
    void translate_instance(size_t instance_idx, const Vec3d& vector);
    void translate(const Vec3d &vector) { this->translate(vector(0), vector(1), vector(2)); }
    void translate(coordf_t x, coordf_t y, coordf_t z);
    void scale(const Vec3d &versor);
    void scale(const double s) { this->scale(Vec3d(s, s, s)); }
    void rotate(float angle, const Axis &axis);
    void rotate(float angle, const Vec3d& axis);
    void mirror(const Axis &axis);
    size_t materials_count() const;
    size_t facets_count() const;
    bool needed_repair() const;
    void cut(coordf_t z, Model* model) const;
    void split(ModelObjectPtrs* new_objects);
    void repair();

    double get_min_z() const;
    double get_instance_min_z(size_t instance_idx) const;

    // Called by Print::validate() from the UI thread.
    unsigned int check_instances_print_volume_state(const BoundingBoxf3& print_volume);

    // Print object statistics to console.
    void print_info() const;

protected:
    friend class Print;
    // Clone this ModelObject including its volumes and instances, keep the IDs of the copies equal to the original.
    // Called by Print::apply() to clone the Model / ModelObject hierarchy to the back end for background processing.
    ModelObject*          clone(Model *parent);
    void                  set_model(Model *model) { m_model = model; }

private:
    ModelObject(Model *model) : layer_height_profile_valid(false), m_model(model), origin_translation(Vec3d::Zero()), m_bounding_box_valid(false) {}
    ModelObject(Model *model, const ModelObject &rhs, bool copy_volumes = true);
    explicit ModelObject(ModelObject &rhs) = delete;
    ~ModelObject();
    ModelObject& operator=(ModelObject &rhs) = default;

    // Parent object, owning this ModelObject.
    Model                *m_model;
    // Bounding box, cached.

    mutable BoundingBoxf3 m_bounding_box;
    mutable bool          m_bounding_box_valid;
};

// An object STL, or a modifier volume, over which a different set of parameters shall be applied.
// ModelVolume instances are owned by a ModelObject.
class ModelVolume : public ModelBase
{
    friend class ModelObject;

    // The convex hull of this model's mesh.
    TriangleMesh        m_convex_hull;

public:
    std::string         name;
    // The triangular model.
    TriangleMesh        mesh;
    // Configuration parameters specific to an object model geometry or a modifier volume, 
    // overriding the global Slic3r settings and the ModelObject settings.
    DynamicPrintConfig  config;

    enum Type {
        MODEL_TYPE_INVALID = -1,
        MODEL_PART = 0,
        PARAMETER_MODIFIER,
        SUPPORT_ENFORCER,
        SUPPORT_BLOCKER,
    };

    // Clone this ModelVolume, keep the ID identical, set the parent to the cloned volume.
    ModelVolume*        clone(ModelObject *parent) { return new ModelVolume(parent, *this); }

    // A parent object owning this modifier volume.
    ModelObject*        get_object() const { return this->object; };
    Type                type() const { return m_type; }
    void                set_type(const Type t) { m_type = t; }
    bool                is_model_part()         const { return m_type == MODEL_PART; }
    bool                is_modifier()           const { return m_type == PARAMETER_MODIFIER; }
    bool                is_support_enforcer()   const { return m_type == SUPPORT_ENFORCER; }
    bool                is_support_blocker()    const { return m_type == SUPPORT_BLOCKER; }
    bool                is_support_modifier()   const { return m_type == SUPPORT_BLOCKER || m_type == SUPPORT_ENFORCER; }
    t_model_material_id material_id() const { return m_material_id; }
    void                set_material_id(t_model_material_id material_id);
    ModelMaterial*      material() const;
    void                set_material(t_model_material_id material_id, const ModelMaterial &material);
    // Split this volume, append the result to the object owning this volume.
    // Return the number of volumes created from this one.
    // This is useful to assign different materials to different volumes of an object.
    size_t              split(unsigned int max_extruders);

    ModelMaterial*      assign_unique_material();
    
    void                calculate_convex_hull();
    const TriangleMesh& get_convex_hull() const;
    TriangleMesh&       get_convex_hull();

    // Helpers for loading / storing into AMF / 3MF files.
    static Type         type_from_string(const std::string &s);
    static std::string  type_to_string(const Type t);

private:
    // Parent object owning this ModelVolume.
    ModelObject*            object;
    // Is it an object to be printed, or a modifier volume?
    Type                    m_type;
    t_model_material_id     m_material_id;
    
    ModelVolume(ModelObject *object, const TriangleMesh &mesh) : mesh(mesh), m_type(MODEL_PART), object(object)
    {
        if (mesh.stl.stats.number_of_facets > 1)
            calculate_convex_hull();
    }
    ModelVolume(ModelObject *object, TriangleMesh &&mesh, TriangleMesh &&convex_hull) : 
        mesh(std::move(mesh)), m_convex_hull(std::move(convex_hull)), m_type(MODEL_PART), object(object) {}
    ModelVolume(ModelObject *object, const ModelVolume &other) :
        ModelBase(other), // copy the ID
        name(other.name), mesh(other.mesh), m_convex_hull(other.m_convex_hull), config(other.config), m_type(other.m_type), object(object)
    {
        this->set_material_id(other.material_id());
    }
    ModelVolume(ModelObject *object, const ModelVolume &other, const TriangleMesh &&mesh) :
        ModelBase(other), // copy the ID
        name(other.name), mesh(std::move(mesh)), config(other.config), m_type(other.m_type), object(object)
    {
        this->set_material_id(other.material_id());
        if (mesh.stl.stats.number_of_facets > 1)
            calculate_convex_hull();
    }

    explicit ModelVolume(ModelVolume &rhs) = delete;
    ModelVolume& operator=(ModelVolume &rhs) = delete;
};

// A single instance of a ModelObject.
// Knows the affine transformation of an object.
class ModelInstance : public ModelBase
{
public:
    enum EPrintVolumeState : unsigned char
    {
        PVS_Inside,
        PVS_Partly_Outside,
        PVS_Fully_Outside,
        Num_BedStates
    };

    friend class ModelObject;

private:
    Vec3d m_offset;              // in unscaled coordinates
    Vec3d m_rotation;            // Rotation around the three axes, in radians around mesh center point
    Vec3d m_scaling_factor;      // Scaling factors along the three axes
#if ENABLE_MIRROR
    Vec3d m_mirror;              // Mirroring along the three axes
#endif // ENABLE_MIRROR

public:
    // flag showing the position of this instance with respect to the print volume (set by Print::validate() using ModelObject::check_instances_print_volume_state())
    EPrintVolumeState print_volume_state;

    ModelObject* get_object() const { return this->object; }

    const Vec3d& get_offset() const { return m_offset; }
    double get_offset(Axis axis) const { return m_offset(axis); }

    void set_offset(const Vec3d& offset) { m_offset = offset; }
    void set_offset(Axis axis, double offset) { m_offset(axis) = offset; }

    const Vec3d& get_rotation() const { return m_rotation; }
    double get_rotation(Axis axis) const { return m_rotation(axis); }

    void set_rotation(const Vec3d& rotation);
    void set_rotation(Axis axis, double rotation);

    Vec3d get_scaling_factor() const { return m_scaling_factor; }
    double get_scaling_factor(Axis axis) const { return m_scaling_factor(axis); }

#if ENABLE_MIRROR
    void set_scaling_factor(const Vec3d& scaling_factor);
    void set_scaling_factor(Axis axis, double scaling_factor);
#else
    void set_scaling_factor(const Vec3d& scaling_factor) { m_scaling_factor = scaling_factor; }
    void set_scaling_factor(Axis axis, double scaling_factor) { m_scaling_factor(axis) = scaling_factor; }
#endif // ENABLE_MIRROR

#if ENABLE_MIRROR
    const Vec3d& get_mirror() const { return m_mirror; }
    double get_mirror(Axis axis) const { return m_mirror(axis); }

    void set_mirror(const Vec3d& mirror);
    void set_mirror(Axis axis, double mirror);
#endif // ENABLE_MIRROR

    // To be called on an external mesh
    void transform_mesh(TriangleMesh* mesh, bool dont_translate = false) const;
    // Calculate a bounding box of a transformed mesh. To be called on an external mesh.
    BoundingBoxf3 transform_mesh_bounding_box(const TriangleMesh* mesh, bool dont_translate = false) const;
    // Transform an external bounding box.
    BoundingBoxf3 transform_bounding_box(const BoundingBoxf3 &bbox, bool dont_translate = false) const;
    // Transform an external vector.
    Vec3d transform_vector(const Vec3d& v, bool dont_translate = false) const;
    // To be called on an external polygon. It does not translate the polygon, only rotates and scales.
    void transform_polygon(Polygon* polygon) const;

#if ENABLE_MIRROR
    Transform3d world_matrix(bool dont_translate = false, bool dont_rotate = false, bool dont_scale = false, bool dont_mirror = false) const;
#else
    Transform3d world_matrix(bool dont_translate = false, bool dont_rotate = false, bool dont_scale = false) const;
#endif // ENABLE_MIRROR

    bool is_printable() const { return print_volume_state == PVS_Inside; }

private:
    // Parent object, owning this instance.
    ModelObject* object;

#if ENABLE_MIRROR
    ModelInstance(ModelObject *object) : m_offset(Vec3d::Zero()), m_rotation(Vec3d::Zero()), m_scaling_factor(Vec3d::Ones()), m_mirror(Vec3d::Ones()), object(object), print_volume_state(PVS_Inside) {}
    ModelInstance(ModelObject *object, const ModelInstance &other) :
        m_offset(other.m_offset), m_rotation(other.m_rotation), m_scaling_factor(other.m_scaling_factor), m_mirror(other.m_mirror), object(object), print_volume_state(PVS_Inside) {}
#else
    ModelInstance(ModelObject *object) : m_rotation(Vec3d::Zero()), m_scaling_factor(Vec3d::Ones()), m_offset(Vec3d::Zero()), object(object), print_volume_state(PVS_Inside) {}
    ModelInstance(ModelObject *object, const ModelInstance &other) :
        m_rotation(other.m_rotation), m_scaling_factor(other.m_scaling_factor), m_offset(other.m_offset), object(object), print_volume_state(PVS_Inside) {}
#endif // ENABLE_MIRROR

    explicit ModelInstance(ModelInstance &rhs) = delete;
    ModelInstance& operator=(ModelInstance &rhs) = delete;
};

// The print bed content.
// Description of a triangular model with multiple materials, multiple instances with various affine transformations
// and with multiple modifier meshes.
// A model groups multiple objects, each object having possibly multiple instances,
// all objects may share mutliple materials.
class Model : public ModelBase
{
    static unsigned int s_auto_extruder_id;

public:
    // Materials are owned by a model and referenced by objects through t_model_material_id.
    // Single material may be shared by multiple models.
    ModelMaterialMap materials;
    // Objects are owned by a model. Each model may have multiple instances, each instance having its own transformation (shift, scale, rotation).
    ModelObjectPtrs objects;
    
    Model() {}
    Model(const Model &rhs);
    Model& operator=(const Model &rhs);
    ~Model() { this->clear_objects(); this->clear_materials(); }

    // XXX: use fs::path ?
    static Model read_from_file(const std::string &input_file, DynamicPrintConfig *config = nullptr, bool add_default_instances = true);
    static Model read_from_archive(const std::string &input_file, DynamicPrintConfig *config, bool add_default_instances = true);

    /// Repair the ModelObjects of the current Model.
    /// This function calls repair function on each TriangleMesh of each model object volume
    void repair();

    ModelObject* add_object();
    ModelObject* add_object(const char *name, const char *path, const TriangleMesh &mesh);
    ModelObject* add_object(const char *name, const char *path, TriangleMesh &&mesh);
    ModelObject* add_object(const ModelObject &other, bool copy_volumes = true);
    void delete_object(size_t idx);
    void delete_object(ModelObject* object);
    void clear_objects();
    
    ModelMaterial* add_material(t_model_material_id material_id);
    ModelMaterial* add_material(t_model_material_id material_id, const ModelMaterial &other);
    ModelMaterial* get_material(t_model_material_id material_id) {
        ModelMaterialMap::iterator i = this->materials.find(material_id);
        return (i == this->materials.end()) ? nullptr : i->second;
    }

    void delete_material(t_model_material_id material_id);
    void clear_materials();
    bool add_default_instances();
    // Returns approximate axis aligned bounding box of this model
    BoundingBoxf3 bounding_box() const;
    // Set the print_volume_state of PrintObject::instances, 
    // return total number of printable objects.
    unsigned int update_print_volume_state(const BoundingBoxf3 &print_volume);
    void center_instances_around_point(const Vec2d &point);
    void translate(coordf_t x, coordf_t y, coordf_t z) { for (ModelObject *o : this->objects) o->translate(x, y, z); }
    TriangleMesh mesh() const;
    bool arrange_objects(coordf_t dist, const BoundingBoxf* bb = NULL);
    // Croaks if the duplicated objects do not fit the print bed.
    void duplicate(size_t copies_num, coordf_t dist, const BoundingBoxf* bb = NULL);
    void duplicate_objects(size_t copies_num, coordf_t dist, const BoundingBoxf* bb = NULL);
    void duplicate_objects_grid(size_t x, size_t y, coordf_t dist);

    bool looks_like_multipart_object() const;
    void convert_multipart_object(unsigned int max_extruders);

    // Ensures that the min z of the model is not negative
    void adjust_min_z();

    void print_info() const { for (const ModelObject *o : this->objects) o->print_info(); }

    static unsigned int get_auto_extruder_id(unsigned int max_extruders);
    static std::string get_auto_extruder_id_as_string(unsigned int max_extruders);
    static void reset_auto_extruder_id();
};

}

#endif
