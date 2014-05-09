#include "Model.hpp"

namespace Slic3r {

Model::Model() {}

Model::Model(const Model &other)
{
    // copy materials
    for (ModelMaterialMap::const_iterator i = other.materials.begin(); i != other.materials.end(); ++i)
        this->add_material(i->first, *i->second);
    
    // copy objects
    this->objects.reserve(other.objects.size());
    for (ModelObjectPtrs::const_iterator i = other.objects.begin(); i != other.objects.end(); ++i)
        this->add_object(**i);
}

Model& Model::operator= (Model other)
{
    this->swap(other);
    return *this;
}

void
Model::swap(Model &other)
{
    std::swap(this->materials,  other.materials);
    std::swap(this->objects,    other.objects);
}

Model::~Model()
{
    this->clear_objects();
    this->clear_materials();
}

ModelObject*
Model::add_object()
{
    ModelObject* new_object = new ModelObject(this);
    this->objects.push_back(new_object);
    return new_object;
}

ModelObject*
Model::add_object(const ModelObject &other)
{
    ModelObject* new_object = new ModelObject(this, other);
    this->objects.push_back(new_object);
    return new_object;
}

void
Model::delete_object(size_t idx)
{
    ModelObjectPtrs::iterator i = this->objects.begin() + idx;
    delete *i;
    this->objects.erase(i);
}

void
Model::clear_objects()
{
    // int instead of size_t because it can be -1 when vector is empty
    for (int i = this->objects.size()-1; i >= 0; --i)
        this->delete_object(i);
}

void
Model::delete_material(t_model_material_id material_id)
{
    ModelMaterialMap::iterator i = this->materials.find(material_id);
    if (i != this->materials.end()) {
        delete i->second;
        this->materials.erase(i);
    }
}

void
Model::clear_materials()
{
    while (!this->materials.empty())
        this->delete_material( this->materials.begin()->first );
}

ModelMaterial*
Model::add_material(t_model_material_id material_id)
{
    ModelMaterial* material = this->get_material(material_id);
    if (material == NULL) {
        material = this->materials[material_id] = new ModelMaterial(this);
    }
    return material;
}

ModelMaterial*
Model::add_material(t_model_material_id material_id, const ModelMaterial &other)
{
    // delete existing material if any
    ModelMaterial* material = this->get_material(material_id);
    if (material != NULL) {
        delete material;
    }
    
    // set new material
    material = new ModelMaterial(this, other);
    this->materials[material_id] = material;
    return material;
}

ModelMaterial*
Model::get_material(t_model_material_id material_id)
{
    ModelMaterialMap::iterator i = this->materials.find(material_id);
    if (i == this->materials.end()) {
        return NULL;
    } else {
        return i->second;
    }
}

/*
void
Model::duplicate_objects_grid(unsigned int x, unsigned int y, coordf_t distance)
{
    if (this->objects.size() > 1) throw "Grid duplication is not supported with multiple objects";
    if (this->objects.empty()) throw "No objects!";

    ModelObject* object = this->objects.front();
    object->clear_instances();

    BoundingBoxf3 bb;
    object->bounding_box(&bb);
    Sizef3 size = bb.size();

    for (unsigned int x_copy = 1; x_copy <= x; ++x_copy) {
        for (unsigned int y_copy = 1; y_copy <= y; ++y_copy) {
            ModelInstance* instance = object->add_instance();
            instance->offset.x = (size.x + distance) * (x_copy-1);
            instance->offset.y = (size.y + distance) * (y_copy-1);
        }
    }
}
*/

bool
Model::has_objects_with_no_instances() const
{
    for (ModelObjectPtrs::const_iterator i = this->objects.begin();
        i != this->objects.end(); ++i)
    {
        if ((*i)->instances.empty()) {
            return true;
        }
    }

    return false;
}

#ifdef SLIC3RXS
REGISTER_CLASS(Model, "Model");
#endif


ModelMaterial::ModelMaterial(Model *model) : model(model) {}
ModelMaterial::ModelMaterial(Model *model, const ModelMaterial &other)
    : model(model), config(other.config), attributes(other.attributes)
{}

void
ModelMaterial::apply(const t_model_material_attributes &attributes)
{
    this->attributes.insert(attributes.begin(), attributes.end());
}


#ifdef SLIC3RXS
REGISTER_CLASS(ModelMaterial, "Model::Material");
#endif


ModelObject::ModelObject(Model *model)
    : model(model)
{}

ModelObject::ModelObject(Model *model, const ModelObject &other)
:   model(model),
    input_file(other.input_file),
    instances(),
    volumes(),
    config(other.config),
    layer_height_ranges(other.layer_height_ranges),
    origin_translation(other.origin_translation),
    _bounding_box(other._bounding_box),
    _bounding_box_valid(other._bounding_box_valid)
{

    this->volumes.reserve(other.volumes.size());
    for (ModelVolumePtrs::const_iterator i = other.volumes.begin(); i != other.volumes.end(); ++i)
        this->add_volume(**i);

    this->instances.reserve(other.instances.size());
    for (ModelInstancePtrs::const_iterator i = other.instances.begin(); i != other.instances.end(); ++i)
        this->add_instance(**i);
}

ModelObject& ModelObject::operator= (ModelObject other)
{
    this->swap(other);
    return *this;
}

void
ModelObject::swap(ModelObject &other)
{
    std::swap(this->input_file,             other.input_file);
    std::swap(this->instances,              other.instances);
    std::swap(this->volumes,                other.volumes);
    std::swap(this->config,                 other.config);
    std::swap(this->layer_height_ranges,    other.layer_height_ranges);
    std::swap(this->origin_translation,     other.origin_translation);
    std::swap(this->_bounding_box,          other._bounding_box);
    std::swap(this->_bounding_box_valid,    other._bounding_box_valid);
}

ModelObject::~ModelObject()
{
    this->clear_volumes();
    this->clear_instances();
}

ModelVolume*
ModelObject::add_volume(const TriangleMesh &mesh)
{
    ModelVolume* v = new ModelVolume(this, mesh);
    this->volumes.push_back(v);
    this->invalidate_bounding_box();
    return v;
}

ModelVolume*
ModelObject::add_volume(const ModelVolume &other)
{
    ModelVolume* v = new ModelVolume(this, other);
    this->volumes.push_back(v);
    this->invalidate_bounding_box();
    return v;
}

void
ModelObject::delete_volume(size_t idx)
{
    ModelVolumePtrs::iterator i = this->volumes.begin() + idx;
    delete *i;
    this->volumes.erase(i);
    this->invalidate_bounding_box();
}

void
ModelObject::clear_volumes()
{
    // int instead of size_t because it can be -1 when vector is empty
    for (int i = this->volumes.size()-1; i >= 0; --i)
        this->delete_volume(i);
}

ModelInstance*
ModelObject::add_instance()
{
    ModelInstance* i = new ModelInstance(this);
    this->instances.push_back(i);
    this->invalidate_bounding_box();
    return i;
}

ModelInstance*
ModelObject::add_instance(const ModelInstance &other)
{
    ModelInstance* i = new ModelInstance(this, other);
    this->instances.push_back(i);
    this->invalidate_bounding_box();
    return i;
}

void
ModelObject::delete_instance(size_t idx)
{
    ModelInstancePtrs::iterator i = this->instances.begin() + idx;
    delete *i;
    this->instances.erase(i);
    this->invalidate_bounding_box();
}

void
ModelObject::delete_last_instance()
{
    this->delete_instance(this->instances.size() - 1);
}

void
ModelObject::clear_instances()
{
    for (size_t i = 0; i < this->instances.size(); ++i)
        this->delete_instance(i);
}

void
ModelObject::invalidate_bounding_box()
{
    this->_bounding_box_valid = false;
}

#ifdef SLIC3RXS
REGISTER_CLASS(ModelObject, "Model::Object");
#endif


ModelVolume::ModelVolume(ModelObject* object, const TriangleMesh &mesh)
:   object(object), mesh(mesh), modifier(false)
{}

ModelVolume::ModelVolume(ModelObject* object, const ModelVolume &other)
:   object(object), material_id(other.material_id), mesh(other.mesh), modifier(other.modifier)
{}

#ifdef SLIC3RXS
REGISTER_CLASS(ModelVolume, "Model::Volume");
#endif


ModelInstance::ModelInstance(ModelObject *object)
:   object(object), rotation(0), scaling_factor(1)
{}

ModelInstance::ModelInstance(ModelObject *object, const ModelInstance &other)
:   object(object), rotation(other.rotation), scaling_factor(other.scaling_factor), offset(other.offset)
{}

#ifdef SLIC3RXS
REGISTER_CLASS(ModelInstance, "Model::Instance");
#endif

}
