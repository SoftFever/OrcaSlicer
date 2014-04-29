#include "Model.hpp"
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif


namespace Slic3r {

Model::Model()
{
}

Model::Model(const Model &other)
:   materials(),
    objects()
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
    delete_all_objects();
    delete_all_materials();
}

ModelObject *
Model::add_object(std::string input_file, DynamicPrintConfig *config,
    t_layer_height_ranges layer_height_ranges, Point origin_translation)
{
    ModelObject *object = new ModelObject(this, input_file, config,
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
Model::delete_all_objects()
{
    for (ModelObjectPtrs::iterator i = this->objects.begin();
        i != this->objects.end(); ++i)
    {
        delete *i;
    }

    objects.clear();
}

void
Model::delete_all_materials()
{
    for (ModelMaterialMap::iterator i = this->materials.begin();
        i != this->materials.end(); ++i)
    {
        delete i->second;
    }

    this->materials.clear();
}

ModelMaterial *
Model::set_material(t_model_material_id material_id,
    const t_model_material_attributes &attributes)
{
    ModelMaterialMap::iterator i = this->materials.find(material_id);

    ModelMaterial *mat;
    if (i == this->materials.end()) {
        mat = this->materials[material_id] = new ModelMaterial(this);
    } else {
        mat = i->second;
    }

    mat->apply(attributes);
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


ModelMaterial::ModelMaterial(Model *model)
:   model(model),
    attributes(),
    config()
{
}

void
ModelMaterial::apply(const t_model_material_attributes &attributes)
{
    this->attributes.insert(attributes.begin(), attributes.end());
}


#ifdef SLIC3RXS
REGISTER_CLASS(ModelMaterial, "Model::Material");
#endif


ModelObject::ModelObject(Model *model, std::string input_file,
    DynamicPrintConfig *config, t_layer_height_ranges layer_height_ranges,
    Point origin_translation)
:   model(model),
    input_file(input_file),
    config(*config),
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
ModelObject::add_volume(t_model_material_id material_id,
        TriangleMesh *mesh, bool modifier)
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
    for (ModelVolumePtrs::iterator i = this->volumes.begin();
        i != this->volumes.end(); ++i)
    {
        delete *i;
    }

    this->volumes.clear();
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
ModelObject::delete_last_instance()
{
    delete this->instances.back();
    this->instances.pop_back();
    this->invalidate_bounding_box();
}

void
ModelObject::clear_instances()
{
    for (ModelInstancePtrs::iterator i = this->instances.begin();
        i != this->instances.end(); ++i)
    {
        delete *i;
    }

    this->instances.clear();
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


ModelVolume::ModelVolume(ModelObject *object, t_model_material_id material_id,
    TriangleMesh *mesh, bool modifier)
:   object(object),
    material_id(material_id),
    mesh(*mesh),
    modifier(modifier)
{
}

ModelVolume::~ModelVolume()
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
    double scaling_factor, Pointf offset)
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
