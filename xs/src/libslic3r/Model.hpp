#ifndef slic3r_Model_hpp_
#define slic3r_Model_hpp_

#include <myinit.h>
#include "PrintConfig.hpp"
#include "Layer.hpp"
#include "Point.hpp"
#include "TriangleMesh.hpp"
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

class Model
{
    public:
    ModelMaterialMap materials;
    ModelObjectPtrs objects;
    
    Model();
    Model(const Model &other);
    Model& operator= (Model other);
    void swap(Model &other);
    ~Model();
    ModelObject* add_object();
    ModelObject* add_object(const ModelObject &other);
    void delete_object(size_t idx);
    void clear_objects();
    
    ModelMaterial* add_material(t_model_material_id material_id);
    ModelMaterial* add_material(t_model_material_id material_id, const ModelMaterial &other);
    ModelMaterial* get_material(t_model_material_id material_id);
    void delete_material(t_model_material_id material_id);
    void clear_materials();
    // void duplicate_objects_grid(unsigned int x, unsigned int y, coordf_t distance);
    // void duplicate_objects(size_t copies_num, coordf_t distance, const BoundingBox &bb);
    // void arrange_objects(coordf_t distance, const BoundingBox &bb);
    // void duplicate(size_t copies_num, coordf_t distance, const BoundingBox &bb);
    bool has_objects_with_no_instances() const;
    // void bounding_box(BoundingBox* bb) const;
    // void align_to_origin();
    // void center_instances_around_point(const Pointf &point);
    // void translate(coordf_t x, coordf_t y, coordf_t z);
    // void mesh(TriangleMesh* mesh) const;
    // void split_meshes();
    // std::string get_material_name(t_model_material_id material_id);

    
    private:
    void _arrange(const std::vector<Size> &sizes, coordf_t distance, const BoundingBox &bb) const;
};

class ModelMaterial
{
    friend class Model;
    public:
    t_model_material_attributes attributes;
    DynamicPrintConfig config;

    Model* get_model() const { return this->model; };
    void apply(const t_model_material_attributes &attributes);
    
    private:
    Model* model;
    
    ModelMaterial(Model *model);
    ModelMaterial(Model *model, const ModelMaterial &other);
};

class ModelObject
{
    friend class Model;
    public:
    std::string name;
    std::string input_file;
    ModelInstancePtrs instances;
    ModelVolumePtrs volumes;
    DynamicPrintConfig config;
    t_layer_height_ranges layer_height_ranges;
    Pointf origin_translation;
    
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

    void invalidate_bounding_box();

    void raw_mesh(TriangleMesh* mesh) const;
    //void mesh(TriangleMesh* mesh) const;
    //void instance_bounding_box(size_t instance_idx, BoundingBox* bb) const;
    //void center_around_origin();
    //void translate(coordf_t x, coordf_t y, coordf_t z);
    //size_t materials_count() const;
    //void unique_materials(std::vector<t_model_material_id>* materials) const;
    //size_t facets_count() const;
    //bool needed_repair() const;
    
    private:
    Model* model;
    
    ModelObject(Model *model);
    ModelObject(Model *model, const ModelObject &other);
    ModelObject& operator= (ModelObject other);
    void swap(ModelObject &other);
    ~ModelObject();
    void update_bounding_box();
};

class ModelVolume
{
    friend class ModelObject;
    public:
    std::string name;
    TriangleMesh mesh;
    DynamicPrintConfig config;
    bool modifier;
    
    ModelObject* get_object() const { return this->object; };
    t_model_material_id material_id() const;
    void material_id(t_model_material_id material_id);
    ModelMaterial* material() const;
    
    ModelMaterial* assign_unique_material();
    
    private:
    ModelObject* object;
    t_model_material_id _material_id;
    
    ModelVolume(ModelObject *object, const TriangleMesh &mesh);
    ModelVolume(ModelObject *object, const ModelVolume &other);
};

class ModelInstance
{
    friend class ModelObject;
    public:
    double rotation;            // around mesh center point
    double scaling_factor;
    Pointf offset;              // in unscaled coordinates
    
    ModelObject* get_object() const { return this->object; };
    void transform_mesh(TriangleMesh* mesh, bool dont_translate = false) const;
    void transform_polygon(Polygon* polygon) const;
    
    private:
    ModelObject* object;
    
    ModelInstance(ModelObject *object);
    ModelInstance(ModelObject *object, const ModelInstance &other);
};

}

#endif
