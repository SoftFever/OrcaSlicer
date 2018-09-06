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

typedef std::string t_model_material_id;
typedef std::string t_model_material_attribute;
typedef std::map<t_model_material_attribute,std::string> t_model_material_attributes;

typedef std::map<t_model_material_id,ModelMaterial*> ModelMaterialMap;
typedef std::vector<ModelObject*> ModelObjectPtrs;
typedef std::vector<ModelVolume*> ModelVolumePtrs;
typedef std::vector<ModelInstance*> ModelInstancePtrs;

// Material, which may be shared across multiple ModelObjects of a single Model.
class ModelMaterial
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
};

// A printable object, possibly having multiple print volumes (each with its own set of parameters and materials),
// and possibly having multiple modifier volumes, each modifier volume with its set of parameters and materials.
// Each ModelObject may be instantiated mutliple times, each instance having different placement on the print bed,
// different rotation and different uniform scaling.
class ModelObject
{
    friend class Model;
public:
    std::string             name;
    std::string             input_file;
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

    /* This vector accumulates the total translation applied to the object by the
        center_around_origin() method. Callers might want to apply the same translation
        to new volumes before adding them to this object in order to preserve alignment
        when user expects that. */
    Pointf3                 origin_translation;
    
    Model* get_model() const { return m_model; };
    
    ModelVolume* add_volume(const TriangleMesh &mesh);
    ModelVolume* add_volume(TriangleMesh &&mesh);
    ModelVolume* add_volume(const ModelVolume &volume);
    void delete_volume(size_t idx);
    void clear_volumes();

    ModelInstance* add_instance();
    ModelInstance* add_instance(const ModelInstance &instance);
    void delete_instance(size_t idx);
    void delete_last_instance();
    void clear_instances();

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
    void translate(const Vectorf3 &vector) { this->translate(vector.x, vector.y, vector.z); }
    void translate(coordf_t x, coordf_t y, coordf_t z);
    void scale(const Pointf3 &versor);
    void rotate(float angle, const Pointf3& axis);
    void transform(const float* matrix3x4);
    void mirror(const Axis &axis);
    size_t materials_count() const;
    size_t facets_count() const;
    bool needed_repair() const;
    void cut(coordf_t z, Model* model) const;
    void split(ModelObjectPtrs* new_objects);

    void check_instances_print_volume_state(const BoundingBoxf3& print_volume);

    // Print object statistics to console.
    void print_info() const;
    
private:        
    ModelObject(Model *model) : layer_height_profile_valid(false), m_model(model), m_bounding_box_valid(false) {}
    ModelObject(Model *model, const ModelObject &other, bool copy_volumes = true);
    ModelObject& operator= (ModelObject other);
    void swap(ModelObject &other);
    ~ModelObject();

    // Parent object, owning this ModelObject.
    Model          *m_model;
    // Bounding box, cached.

    mutable BoundingBoxf3 m_bounding_box;
    mutable bool          m_bounding_box_valid;
};

// An object STL, or a modifier volume, over which a different set of parameters shall be applied.
// ModelVolume instances are owned by a ModelObject.
class ModelVolume
{
    friend class ModelObject;

    // The convex hull of this model's mesh.
    TriangleMesh m_convex_hull;

public:
    std::string name;
    // The triangular model.
    TriangleMesh mesh;
    // Configuration parameters specific to an object model geometry or a modifier volume, 
    // overriding the global Slic3r settings and the ModelObject settings.
    DynamicPrintConfig config;

    enum Type {
        MODEL_TYPE_INVALID = -1,
        MODEL_PART = 0,
        PARAMETER_MODIFIER,
        SUPPORT_ENFORCER,
        SUPPORT_BLOCKER,
    };

    // A parent object owning this modifier volume.
    ModelObject*        get_object() const { return this->object; };
    Type                type() const { return m_type; }
    void                set_type(const Type t) { m_type = t; }
    bool                is_model_part()         const { return m_type == MODEL_PART; }
    bool                is_modifier()           const { return m_type == PARAMETER_MODIFIER; }
    bool                is_support_enforcer()   const { return m_type == SUPPORT_ENFORCER; }
    bool                is_support_blocker()    const { return m_type == SUPPORT_BLOCKER; }
    t_model_material_id material_id() const { return this->_material_id; }
    void                material_id(t_model_material_id material_id);
    ModelMaterial*      material() const;
    void                set_material(t_model_material_id material_id, const ModelMaterial &material);
    // Split this volume, append the result to the object owning this volume.
    // Return the number of volumes created from this one.
    // This is useful to assign different materials to different volumes of an object.
    size_t split(unsigned int max_extruders);

    ModelMaterial* assign_unique_material();
    
    void calculate_convex_hull();
    const TriangleMesh& get_convex_hull() const;

    // Helpers for loading / storing into AMF / 3MF files.
    static Type         type_from_string(const std::string &s);
    static std::string  type_to_string(const Type t);

private:
    // Parent object owning this ModelVolume.
    ModelObject*            object;
    // Is it an object to be printed, or a modifier volume?
    Type                    m_type;
    t_model_material_id     _material_id;
    
    ModelVolume(ModelObject *object, const TriangleMesh &mesh) : mesh(mesh), m_type(MODEL_PART), object(object)
    {
        if (mesh.stl.stats.number_of_facets > 1)
            calculate_convex_hull();
    }
    ModelVolume(ModelObject *object, TriangleMesh &&mesh, TriangleMesh &&convex_hull) : mesh(std::move(mesh)), m_convex_hull(std::move(convex_hull)), m_type(MODEL_PART), object(object) {}
    ModelVolume(ModelObject *object, const ModelVolume &other) :
        name(other.name), mesh(other.mesh), m_convex_hull(other.m_convex_hull), config(other.config), m_type(other.m_type), object(object)
    {
        this->material_id(other.material_id());
    }
    ModelVolume(ModelObject *object, const ModelVolume &other, const TriangleMesh &&mesh) :
        name(other.name), mesh(std::move(mesh)), config(other.config), m_type(other.m_type), object(object)
    {
        this->material_id(other.material_id());
        if (mesh.stl.stats.number_of_facets > 1)
            calculate_convex_hull();
    }
};

// A single instance of a ModelObject.
// Knows the affine transformation of an object.
class ModelInstance
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

    double rotation;            // Rotation around the Z axis, in radians around mesh center point
    double scaling_factor;
    Pointf offset;              // in unscaled coordinates
    
    // flag showing the position of this instance with respect to the print volume (set by Print::validate() using ModelObject::check_instances_print_volume_state())
    EPrintVolumeState print_volume_state;

    ModelObject* get_object() const { return this->object; }

    // To be called on an external mesh
    void transform_mesh(TriangleMesh* mesh, bool dont_translate = false) const;
    // Calculate a bounding box of a transformed mesh. To be called on an external mesh.
    BoundingBoxf3 transform_mesh_bounding_box(const TriangleMesh* mesh, bool dont_translate = false) const;
    // Transform an external bounding box.
    BoundingBoxf3 transform_bounding_box(const BoundingBoxf3 &bbox, bool dont_translate = false) const;
    // To be called on an external polygon. It does not translate the polygon, only rotates and scales.
    void transform_polygon(Polygon* polygon) const;

    bool is_printable() const { return print_volume_state == PVS_Inside; }

private:
    // Parent object, owning this instance.
    ModelObject* object;

    ModelInstance(ModelObject *object) : rotation(0), scaling_factor(1), object(object), print_volume_state(PVS_Inside) {}
    ModelInstance(ModelObject *object, const ModelInstance &other) :
        rotation(other.rotation), scaling_factor(other.scaling_factor), offset(other.offset), object(object), print_volume_state(PVS_Inside) {}
};


// The print bed content.
// Description of a triangular model with multiple materials, multiple instances with various affine transformations
// and with multiple modifier meshes.
// A model groups multiple objects, each object having possibly multiple instances,
// all objects may share mutliple materials.
class Model
{
    static unsigned int s_auto_extruder_id;

public:
    // Materials are owned by a model and referenced by objects through t_model_material_id.
    // Single material may be shared by multiple models.
    ModelMaterialMap materials;
    // Objects are owned by a model. Each model may have multiple instances, each instance having its own transformation (shift, scale, rotation).
    ModelObjectPtrs objects;
    
    Model() {}
    Model(const Model &other);
    Model& operator= (Model other);
    void swap(Model &other);
    ~Model() { this->clear_objects(); this->clear_materials(); }

    static Model read_from_file(const std::string &input_file, bool add_default_instances = true);
    static Model read_from_archive(const std::string &input_file, PresetBundle* bundle, bool add_default_instances = true);

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
    void center_instances_around_point(const Pointf &point);
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
