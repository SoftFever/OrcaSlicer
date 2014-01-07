#ifndef slic3r_Model_hpp_
#define slic3r_Model_hpp_

#include <myinit.h>
#include "Config.hpp"
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

class Model
{
    public:
    std::map<t_model_material_id,ModelMaterial*> materials;
    std::vector<ModelObject*> objects;
    
    Model();
    ~Model();
    ModelObject* add_object(const ModelObject &object);
    void delete_object(size_t obj_idx);
    void delete_all_objects();
    void set_material(t_model_material_id material_id, const t_model_material_attributes &attributes);
    void duplicate_objects_grid(coordf_t x, coordf_t y, coordf_t distance);
    void duplicate_objects(size_t copies_num, coordf_t distance, const BoundingBox &bb);
    void arrange_objects(coordf_t distance, const BoundingBox &bb);
    void duplicate(size_t copies_num, coordf_t distance, const BoundingBox &bb);
    bool has_objects_with_no_instances() const;
    void bounding_box(BoundingBox* bb) const;
    void align_to_origin();
    void center_instances_around_point(const Pointf &point);
    void translate(coordf_t x, coordf_t y, coordf_t z);
    void mesh(TriangleMesh* mesh) const;
    void split_meshes();
    std::string get_material_name(t_model_material_id material_id);
    
    private:
    void _arrange(const std::vector<Size> &sizes, coordf_t distance, const BoundingBox &bb) const;
};

class ModelMaterial
{
    public:
    Model* model;
    t_model_material_attributes attributes;
    DynamicConfig config;
};

class ModelObject
{
    public:
    Model* model;
    std::string input_file;
    std::vector<ModelInstance*> instances;
    std::vector<ModelVolume*> volumes;
    DynamicConfig config;
    t_layer_height_ranges layer_height_ranges;
    
    ModelObject();
    ~ModelObject();
    ModelInstance* add_instance(const ModelInstance &instance);
    ModelVolume* add_volume(const ModelVolume &volume);
    void delete_last_instance();
    void raw_mesh(TriangleMesh* mesh) const;
    void mesh(TriangleMesh* mesh) const;
    void instance_bounding_box(size_t instance_idx, BoundingBox* bb) const;
    void center_around_origin();
    void translate(coordf_t x, coordf_t y, coordf_t z);
    size_t materials_count() const;
    void unique_materials(std::vector<t_model_material_id>* materials) const;
    size_t facets_count() const;
    bool needed_repair() const;
    
    private:
    BoundingBox bb;
    void update_bounding_box();
};

class ModelVolume
{
    public:
    ModelObject* object;
    t_model_material_id material_id;
    TriangleMesh mesh;
};

class ModelInstance
{
    public:
    ModelObject* object;
    double rotation;
    double scaling_factor;
    Pointf offset;
    
    void transform_mesh(TriangleMesh* mesh, bool dont_translate) const;
    void transform_polygon(Polygon* polygon) const;
};

}

#endif
