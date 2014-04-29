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
    ~Model();
    ModelObject *add_object(std::string input_file, DynamicPrintConfig *config,
        t_layer_height_ranges layer_height_ranges, Point origin_translation);
    void delete_object(size_t idx);
    void delete_all_objects();
    void delete_all_materials();
    ModelMaterial *set_material(t_model_material_id material_id,
        const t_model_material_attributes &attributes);
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
    public:
    Model* model;
    t_model_material_attributes attributes;
    DynamicPrintConfig config;

    ModelMaterial(Model *model);
    void apply(const t_model_material_attributes &attributes);
};

class ModelObject
{
    public:
    Model* model;
    std::string input_file;
    ModelInstancePtrs instances;
    ModelVolumePtrs volumes;
    DynamicPrintConfig config;
    t_layer_height_ranges layer_height_ranges;
    Point origin_translation;
    BoundingBoxf3 _bounding_box;
    bool _bounding_box_valid;
    
    ModelObject(Model *model, std::string input_file, DynamicPrintConfig *config,
        t_layer_height_ranges layer_height_ranges, Point origin_translation);
    ModelObject(const ModelObject &other);
    ~ModelObject();

    ModelVolume *add_volume(t_model_material_id material_id,
        TriangleMesh *mesh, bool modifier);
    void delete_volume(size_t idx);
    void clear_volumes();

    ModelInstance *add_instance(double rotation=0, double scaling_factor=1,
        Pointf offset=Pointf(0, 0));
    void delete_last_instance();
    void clear_instances();

    void invalidate_bounding_box();

    void raw_mesh(TriangleMesh* mesh) const;
    void mesh(TriangleMesh* mesh) const;
    void instance_bounding_box(size_t instance_idx, BoundingBox* bb) const;
    void center_around_origin();
    void translate(coordf_t x, coordf_t y, coordf_t z);
    size_t materials_count() const;
    void unique_materials(std::vector<t_model_material_id>* materials) const;
    size_t facets_count() const;
    bool needed_repair() const;
    
    #ifdef SLIC3RXS
    SV* to_SV_ref();
    #endif

    private:
    void update_bounding_box();
};

class ModelVolume
{
    public:
    ModelObject* object;
    t_model_material_id material_id;
    TriangleMesh mesh;
    bool modifier;

    ModelVolume(ModelObject *object, t_model_material_id material_id,
        TriangleMesh *mesh, bool modifier);
    ~ModelVolume();

    #ifdef SLIC3RXS
    SV* to_SV_ref();
    #endif
};

class ModelInstance
{
    public:
    ModelObject* object;
    double rotation;            // around mesh center point
    double scaling_factor;
    Pointf offset;              // in unscaled coordinates

    ModelInstance(ModelObject *object, double rotation, double scaling_factor,
        Pointf offset);
    ~ModelInstance();
    
    void transform_mesh(TriangleMesh* mesh, bool dont_translate) const;
    void transform_polygon(Polygon* polygon) const;

    #ifdef SLIC3RXS
    SV* to_SV_ref();
    #endif
};

}

#endif
