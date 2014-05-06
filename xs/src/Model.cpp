#include "Model.hpp"
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif


namespace Slic3r {

Model::Model() {}

Model::Model(const Model &other)
{
    objects.reserve(other.objects.size());

    for (ModelMaterialMap::const_iterator i = other.materials.begin();
        i != other.materials.end(); ++i)
    {
        ModelMaterial *copy = new ModelMaterial(*i->second);
        copy->model = this;
        materials[i->first] = copy;

    }

    for (ModelObjectPtrs::const_iterator i = other.objects.begin();
        i != other.objects.end(); ++i)
    {
        ModelObject *copy = new ModelObject(**i);
        copy->model = this;
        objects.push_back(copy);
    }
}

Model::~Model()
{
    this->clear_objects();
    this->clear_materials();
}

ModelObject*
Model::add_object(const std::string &input_file, const DynamicPrintConfig &config,
    const t_layer_height_ranges &layer_height_ranges, const Pointf &origin_translation)
{
    ModelObject* object = new ModelObject(this, input_file, config,
        layer_height_ranges, origin_translation);
    this->objects.push_back(object);
    return object;
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
    for (size_t i = 0; i < this->objects.size(); ++i)
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

ModelMaterial *
Model::set_material(t_model_material_id material_id)
{
    ModelMaterialMap::iterator i = this->materials.find(material_id);
    
    ModelMaterial *mat;
    if (i == this->materials.end()) {
        mat = this->materials[material_id] = new ModelMaterial(this);
    } else {
        mat = i->second;
    }
    
    return mat;
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

void
ModelMaterial::apply(const t_model_material_attributes &attributes)
{
    this->attributes.insert(attributes.begin(), attributes.end());
}


#ifdef SLIC3RXS
REGISTER_CLASS(ModelMaterial, "Model::Material");
#endif


ModelObject::ModelObject(Model *model, const std::string &input_file,
    const DynamicPrintConfig &config, const t_layer_height_ranges &layer_height_ranges,
    const Pointf &origin_translation)
:   model(model),
    input_file(input_file),
    config(config),
    layer_height_ranges(layer_height_ranges),
    origin_translation(origin_translation),
    _bounding_box_valid(false)
{
}

ModelObject::ModelObject(const ModelObject &other)
:   model(other.model),
    input_file(other.input_file),
    instances(),
    volumes(),
    config(other.config),
    layer_height_ranges(other.layer_height_ranges),
    origin_translation(other.origin_translation),
    _bounding_box(other._bounding_box),
    _bounding_box_valid(other._bounding_box_valid)
{
    volumes.reserve(other.volumes.size());
    instances.reserve(other.instances.size());

    for (ModelVolumePtrs::const_iterator i = other.volumes.begin();
        i != other.volumes.end(); ++i)
    {
        ModelVolume *v = new ModelVolume(**i);
        v->object = this;
        volumes.push_back(v);

    }

    for (ModelInstancePtrs::const_iterator i = other.instances.begin();
        i != other.instances.end(); ++i)
    {
        ModelInstance *in = new ModelInstance(**i);
        in->object = this;
        instances.push_back(in);
    }
}

ModelObject::~ModelObject()
{
    this->clear_volumes();
    this->clear_instances();
}

ModelVolume *
ModelObject::add_volume(const t_model_material_id &material_id,
        const TriangleMesh &mesh, bool modifier)
{
    ModelVolume *v = new ModelVolume(this, material_id, mesh, modifier);
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
    for (size_t i = 0; i < this->volumes.size(); ++i)
        this->delete_volume(i);
}

ModelInstance *
ModelObject::add_instance(double rotation, double scaling_factor,
    Pointf offset)
{
    ModelInstance *i = new ModelInstance(
        this, rotation, scaling_factor, offset);
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

SV*
ModelObject::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name_ref(this), this );
    return sv;
}
#endif


ModelVolume::ModelVolume(ModelObject* object, const t_model_material_id &material_id,
    const TriangleMesh &mesh, bool modifier)
:   object(object),
    material_id(material_id),
    mesh(mesh),
    modifier(modifier)
{
}

#ifdef SLIC3RXS
REGISTER_CLASS(ModelVolume, "Model::Volume");

SV*
ModelVolume::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name_ref(this), this );
    return sv;
}
#endif


ModelInstance::ModelInstance(ModelObject *object, double rotation,
    double scaling_factor, const Pointf &offset)
:   object(object),
    rotation(rotation),
    scaling_factor(scaling_factor),
    offset(offset)
{
}

ModelInstance::~ModelInstance()
{
}

#ifdef SLIC3RXS
REGISTER_CLASS(ModelInstance, "Model::Instance");

SV*
ModelInstance::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name_ref(this), this );
    return sv;
}
#endif

}
