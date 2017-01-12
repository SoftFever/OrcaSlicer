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

class ModelInstance;
class ModelMaterial;
class ModelObject;
class ModelVolume;

typedef std::string t_model_material_id;
typedef std::string t_model_material_attribute;
typedef std::map<t_model_material_attribute,std::string> t_model_material_attributes;

typedef std::map<t_model_material_id,ModelMaterial*> ModelMaterialMap;
typedef std::vector<ModelObject*> ModelObjectPtrs;
typedef std::vector<ModelVolume*> ModelVolumePtrs;
typedef std::vector<ModelInstance*> ModelInstancePtrs;

// The print bed content.
// Description of a triangular model with multiple materials, multiple instances with various affine transformations
// and with multiple modifier meshes.
// A model groups multiple objects, each object having possibly multiple instances,
// all objects may share mutliple materials.
class Model
{
public:
    // Materials are owned by a model and referenced by objects through t_model_material_id.
    // Single material may be shared by multiple models.
    ModelMaterialMap materials;
    // Objects are owned by a model. Each model may have multiple instances, each instance having its own transformation (shift, scale, rotation).
    ModelObjectPtrs objects;
    
    Model();
    Model(const Model &other);
    Model& operator= (Model other);
    void swap(Model &other);
    ~Model();
    ModelObject* add_object();
    ModelObject* add_object(const ModelObject &other, bool copy_volumes = true);
    void delete_object(size_t idx);
    void clear_objects();
    
    ModelMaterial* add_material(t_model_material_id material_id);
    ModelMaterial* add_material(t_model_material_id material_id, const ModelMaterial &other);
    ModelMaterial* get_material(t_model_material_id material_id);
    void delete_material(t_model_material_id material_id);
    void clear_materials();
    bool has_objects_with_no_instances() const;
    bool add_default_instances();
    BoundingBoxf3 bounding_box() const;
    void center_instances_around_point(const Pointf &point);
    void align_instances_to_origin();
    void translate(coordf_t x, coordf_t y, coordf_t z);
    TriangleMesh mesh() const;
    TriangleMesh raw_mesh() const;
    bool _arrange(const Pointfs &sizes, coordf_t dist, const BoundingBoxf* bb, Pointfs &out) const;
    bool arrange_objects(coordf_t dist, const BoundingBoxf* bb = NULL);
    // Croaks if the duplicated objects do not fit the print bed.
    void duplicate(size_t copies_num, coordf_t dist, const BoundingBoxf* bb = NULL);
    void duplicate_objects(size_t copies_num, coordf_t dist, const BoundingBoxf* bb = NULL);
    void duplicate_objects_grid(size_t x, size_t y, coordf_t dist);
};

// Material, which may be shared across multiple ModelObjects of a single Model.
class ModelMaterial
{
    friend class Model;
public:
    // Attributes are defined by the AMF file format, but they don't seem to be used by Slic3r for any purpose.
    t_model_material_attributes attributes;
    // Dynamic configuration storage for the object specific configuration values, overriding the global configuration.
    DynamicPrintConfig config;

    Model* get_model() const { return this->model; };
    void apply(const t_model_material_attributes &attributes);
    
private:
    // Parent, owning this material.
    Model* model;
    
    ModelMaterial(Model *model);
    ModelMaterial(Model *model, const ModelMaterial &other);
};

// A printable object, possibly having multiple print volumes (each with its own set of parameters and materials),
// and possibly having multiple modifier volumes, each modifier volume with its set of parameters and materials.
// Each ModelObject may be instantiated mutliple times, each instance having different placement on the print bed,
// different rotation and different uniform scaling.
class ModelObject
{
    friend class Model;
public:
    std::string name;
    std::string input_file;
    // Instances of this ModelObject. Each instance defines a shift on the print bed, rotation around the Z axis and a uniform scaling.
    // Instances are owned by this ModelObject.
    ModelInstancePtrs instances;
    // Printable and modifier volumes, each with its material ID and a set of override parameters.
    // ModelVolumes are owned by this ModelObject.
    ModelVolumePtrs volumes;
    // Configuration parameters specific to a single ModelObject, overriding the global Slic3r settings.
    DynamicPrintConfig config;
    // Variation of a layer thickness for spans of Z coordinates.
    t_layer_height_ranges layer_height_ranges;

    /* This vector accumulates the total translation applied to the object by the
        center_around_origin() method. Callers might want to apply the same translation
        to new volumes before adding them to this object in order to preserve alignment
        when user expects that. */
    Pointf3 origin_translation;
    
    // these should be private but we need to expose them via XS until all methods are ported
    BoundingBoxf3 _bounding_box;
    bool _bounding_box_valid;
    
    Model* get_model() const { return this->model; };
    
    ModelVolume* add_volume(const TriangleMesh &mesh);
    ModelVolume* add_volume(const ModelVolume &volume);
    void delete_volume(size_t idx);
    void clear_volumes();

    ModelInstance* add_instance();
    ModelInstance* add_instance(const ModelInstance &instance);
    void delete_instance(size_t idx);
    void delete_last_instance();
    void clear_instances();

    BoundingBoxf3 bounding_box();
    void invalidate_bounding_box();

    TriangleMesh mesh() const;
    TriangleMesh raw_mesh() const;
    BoundingBoxf3 raw_bounding_box() const;
    BoundingBoxf3 instance_bounding_box(size_t instance_idx) const;
    void center_around_origin();
    void translate(const Vectorf3 &vector);
    void translate(coordf_t x, coordf_t y, coordf_t z);
    void scale(const Pointf3 &versor);
    void rotate(float angle, const Axis &axis);
    void mirror(const Axis &axis);
    size_t materials_count() const;
    size_t facets_count() const;
    bool needed_repair() const;
    void cut(coordf_t z, Model* model) const;
    void split(ModelObjectPtrs* new_objects);
    void update_bounding_box();   // this is a private method but we expose it until we need to expose it via XS
    
private:
    // Parent object, owning this ModelObject.
    Model* model;
    
    ModelObject(Model *model);
    ModelObject(Model *model, const ModelObject &other, bool copy_volumes = true);
    ModelObject& operator= (ModelObject other);
    void swap(ModelObject &other);
    ~ModelObject();
};

// An object STL, or a modifier volume, over which a different set of parameters shall be applied.
// ModelVolume instances are owned by a ModelObject.
class ModelVolume
{
    friend class ModelObject;
public:
    std::string name;
    // The triangular model.
    TriangleMesh mesh;
    // Configuration parameters specific to an object model geometry or a modifier volume, 
    // overriding the global Slic3r settings and the ModelObject settings.
    DynamicPrintConfig config;
    // Is it an object to be printed, or a modifier volume?
    bool modifier;
    
    // A parent object owning this modifier volume.
    ModelObject* get_object() const { return this->object; };
    t_model_material_id material_id() const;
    void material_id(t_model_material_id material_id);
    ModelMaterial* material() const;
    void set_material(t_model_material_id material_id, const ModelMaterial &material);
    
    ModelMaterial* assign_unique_material();
    
private:
    // Parent object owning this ModelVolume.
    ModelObject* object;
    t_model_material_id _material_id;
    
    ModelVolume(ModelObject *object, const TriangleMesh &mesh);
    ModelVolume(ModelObject *object, const ModelVolume &other);
};

// A single instance of a ModelObject.
// Knows the affine transformation of an object.
class ModelInstance
{
    friend class ModelObject;
public:
    double rotation;            // Rotation around the Z axis, in radians around mesh center point
    double scaling_factor;
    Pointf offset;              // in unscaled coordinates
    
    ModelObject* get_object() const { return this->object; };

    // To be called on an external mesh
    void transform_mesh(TriangleMesh* mesh, bool dont_translate = false) const;
    // Calculate a bounding box of a transformed mesh. To be called on an external mesh.
    BoundingBoxf3 transform_mesh_bounding_box(const TriangleMesh* mesh, bool dont_translate = false) const;
    // Transform an external bounding box.
    BoundingBoxf3 transform_bounding_box(const BoundingBoxf3 &bbox, bool dont_translate = false) const;
    // To be called on an external polygon. It does not translate the polygon, only rotates and scales.
    void transform_polygon(Polygon* polygon) const;
    
private:
    // Parent object, owning this instance.
    ModelObject* object;
    
    ModelInstance(ModelObject *object);
    ModelInstance(ModelObject *object, const ModelInstance &other);
};

}

#endif
