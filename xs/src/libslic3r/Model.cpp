#include "Model.hpp"
#include "Geometry.hpp"

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
        this->add_object(**i, true);
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
Model::add_object(const ModelObject &other, bool copy_volumes)
{
    ModelObject* new_object = new ModelObject(this, other, copy_volumes);
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

    BoundingBoxf3 bb = object->bounding_box();
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

// makes sure all objects have at least one instance
bool
Model::add_default_instances()
{
    bool added = false;
    // apply a default position to all objects not having one
    for (ModelObjectPtrs::const_iterator o = this->objects.begin(); o != this->objects.end(); ++o) {
        if ((*o)->instances.empty()) {
            (*o)->add_instance();
            added = true;
        }
    }
    return true;
}

// this returns the bounding box of the *transformed* instances
BoundingBoxf3
Model::bounding_box() const
{
    BoundingBoxf3 bb;
    for (ModelObjectPtrs::const_iterator o = this->objects.begin(); o != this->objects.end(); ++o) {
        bb.merge((*o)->bounding_box());
    }
    return bb;
}

void
Model::center_instances_around_point(const Pointf &point)
{
    BoundingBoxf3 bb = this->bounding_box();
    
    Sizef3 size = bb.size();
    double shift_x = -bb.min.x + point.x - size.x/2;
    double shift_y = -bb.min.y + point.y - size.y/2;
    
    for (ModelObjectPtrs::const_iterator o = this->objects.begin(); o != this->objects.end(); ++o) {
        for (ModelInstancePtrs::const_iterator i = (*o)->instances.begin(); i != (*o)->instances.end(); ++i) {
            (*i)->offset.translate(shift_x, shift_y);
        }
        (*o)->update_bounding_box();
    }
}

void
Model::align_instances_to_origin()
{
    BoundingBoxf3 bb = this->bounding_box();
    
    Pointf new_center = (Pointf)bb.size();
    new_center.translate(-new_center.x/2, -new_center.y/2);
    this->center_instances_around_point(new_center);
}

void
Model::translate(coordf_t x, coordf_t y, coordf_t z)
{
    for (ModelObjectPtrs::const_iterator o = this->objects.begin(); o != this->objects.end(); ++o) {
        (*o)->translate(x, y, z);
    }
}

// flattens everything to a single mesh
TriangleMesh
Model::mesh() const
{
    TriangleMesh mesh;
    for (ModelObjectPtrs::const_iterator o = this->objects.begin(); o != this->objects.end(); ++o) {
        mesh.merge((*o)->mesh());
    }
    return mesh;
}

// flattens everything to a single mesh
TriangleMesh
Model::raw_mesh() const
{
    TriangleMesh mesh;
    for (ModelObjectPtrs::const_iterator o = this->objects.begin(); o != this->objects.end(); ++o) {
        mesh.merge((*o)->raw_mesh());
    }
    return mesh;
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

ModelObject::ModelObject(Model *model, const ModelObject &other, bool copy_volumes)
:   model(model),
    name(other.name),
    input_file(other.input_file),
    instances(),
    volumes(),
    config(other.config),
    layer_height_ranges(other.layer_height_ranges),
    origin_translation(other.origin_translation),
    _bounding_box(other._bounding_box),
    _bounding_box_valid(other._bounding_box_valid)
{
    if (copy_volumes) {
        this->volumes.reserve(other.volumes.size());
        for (ModelVolumePtrs::const_iterator i = other.volumes.begin(); i != other.volumes.end(); ++i)
            this->add_volume(**i);
    }
    
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

// this returns the bounding box of the *transformed* instances
BoundingBoxf3
ModelObject::bounding_box()
{
    if (!this->_bounding_box_valid) this->update_bounding_box();
    return this->_bounding_box;
}

void
ModelObject::invalidate_bounding_box()
{
    this->_bounding_box_valid = false;
}

void
ModelObject::update_bounding_box()
{
    this->_bounding_box = this->mesh().bounding_box();
    this->_bounding_box_valid = true;
}

// flattens all volumes and instances into a single mesh
TriangleMesh
ModelObject::mesh() const
{
    TriangleMesh mesh;
    TriangleMesh raw_mesh = this->raw_mesh();
    
    for (ModelInstancePtrs::const_iterator i = this->instances.begin(); i != this->instances.end(); ++i) {
        TriangleMesh m = raw_mesh;
        (*i)->transform_mesh(&m);
        mesh.merge(m);
    }
    return mesh;
}

TriangleMesh
ModelObject::raw_mesh() const
{
    TriangleMesh mesh;
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        if ((*v)->modifier) continue;
        mesh.merge((*v)->mesh);
    }
    return mesh;
}

BoundingBoxf3
ModelObject::raw_bounding_box() const
{
    BoundingBoxf3 bb;
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        if ((*v)->modifier) continue;
        TriangleMesh mesh = (*v)->mesh;
        
        if (this->instances.empty()) CONFESS("Can't call raw_bounding_box() with no instances");
        this->instances.front()->transform_mesh(&mesh, true);
        
        bb.merge(mesh.bounding_box());
    }
    return bb;
}

// this returns the bounding box of the *transformed* given instance
BoundingBoxf3
ModelObject::instance_bounding_box(size_t instance_idx) const
{
    TriangleMesh mesh = this->raw_mesh();
    this->instances[instance_idx]->transform_mesh(&mesh);
    return mesh.bounding_box();
}

void
ModelObject::center_around_origin()
{
    // calculate the displacements needed to 
    // center this object around the origin
    BoundingBoxf3 bb = this->raw_mesh().bounding_box();
    
    // first align to origin on XYZ
    Vectorf3 vector(-bb.min.x, -bb.min.y, -bb.min.z);
    
    // then center it on XY
    Sizef3 size = bb.size();
    vector.x -= size.x/2;
    vector.y -= size.y/2;
    
    this->translate(vector);
    this->origin_translation.translate(vector);
    
    if (!this->instances.empty()) {
        for (ModelInstancePtrs::const_iterator i = this->instances.begin(); i != this->instances.end(); ++i) {
            // apply rotation and scaling to vector as well before translating instance,
            // in order to leave final position unaltered
            Vectorf3 v = vector.negative();
            v.rotate((*i)->rotation, (*i)->offset);
            v.scale((*i)->scaling_factor);
            (*i)->offset.translate(v.x, v.y);
        }
        this->update_bounding_box();
    }
}

void
ModelObject::translate(const Vectorf3 &vector)
{
    this->translate(vector.x, vector.y, vector.z);
}

void
ModelObject::translate(coordf_t x, coordf_t y, coordf_t z)
{
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        (*v)->mesh.translate(x, y, z);
    }
    if (this->_bounding_box_valid) this->_bounding_box.translate(x, y, z);
}

void
ModelObject::scale(const Pointf3 &versor)
{
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        (*v)->mesh.scale(versor);
    }
    
    // reset origin translation since it doesn't make sense anymore
    this->origin_translation = Pointf3(0,0,0);
    this->invalidate_bounding_box();
}

void
ModelObject::rotate(float angle, const Axis &axis)
{
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        (*v)->mesh.rotate(angle, axis);
    }
    this->origin_translation = Pointf3(0,0,0);
    this->invalidate_bounding_box();
}

void
ModelObject::flip(const Axis &axis)
{
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        (*v)->mesh.flip(axis);
    }
    this->origin_translation = Pointf3(0,0,0);
    this->invalidate_bounding_box();
}

size_t
ModelObject::materials_count() const
{
    std::set<t_model_material_id> material_ids;
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        material_ids.insert((*v)->material_id());
    }
    return material_ids.size();
}

size_t
ModelObject::facets_count() const
{
    size_t num = 0;
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        if ((*v)->modifier) continue;
        num += (*v)->mesh.stl.stats.number_of_facets;
    }
    return num;
}

bool
ModelObject::needed_repair() const
{
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        if ((*v)->modifier) continue;
        if ((*v)->mesh.needed_repair()) return true;
    }
    return false;
}

void
ModelObject::cut(coordf_t z, Model* model) const
{
    // clone this one to duplicate instances, materials etc.
    ModelObject* upper = model->add_object(*this);
    ModelObject* lower = model->add_object(*this);
    upper->clear_volumes();
    lower->clear_volumes();
    
    for (ModelVolumePtrs::const_iterator v = this->volumes.begin(); v != this->volumes.end(); ++v) {
        ModelVolume* volume = *v;
        if (volume->modifier) {
            // don't cut modifiers
            upper->add_volume(*volume);
            lower->add_volume(*volume);
        } else {
            TriangleMeshSlicer tms(&volume->mesh);
            TriangleMesh upper_mesh, lower_mesh;
            // TODO: shouldn't we use object bounding box instead of per-volume bb?
            tms.cut(z + volume->mesh.bounding_box().min.z, &upper_mesh, &lower_mesh);
            upper_mesh.repair();
            lower_mesh.repair();
            upper_mesh.reset_repair_stats();
            lower_mesh.reset_repair_stats();
            
            if (upper_mesh.facets_count() > 0) {
                ModelVolume* vol    = upper->add_volume(upper_mesh);
                vol->name           = volume->name;
                vol->config         = volume->config;
                vol->set_material(volume->material_id(), *volume->material());
            }
            if (lower_mesh.facets_count() > 0) {
                ModelVolume* vol    = lower->add_volume(lower_mesh);
                vol->name           = volume->name;
                vol->config         = volume->config;
                vol->set_material(volume->material_id(), *volume->material());
            }
        }
    }
}

void
ModelObject::split(ModelObjectPtrs* new_objects)
{
    if (this->volumes.size() > 1) {
        // We can't split meshes if there's more than one volume, because
        // we can't group the resulting meshes by object afterwards
        new_objects->push_back(this);
        return;
    }
    
    ModelVolume* volume = this->volumes.front();
    TriangleMeshPtrs meshptrs = volume->mesh.split();
    for (TriangleMeshPtrs::iterator mesh = meshptrs.begin(); mesh != meshptrs.end(); ++mesh) {
        (*mesh)->repair();
        
        ModelObject* new_object = this->model->add_object(*this, false);
        ModelVolume* new_volume = new_object->add_volume(**mesh);
        new_volume->name        = volume->name;
        new_volume->config      = volume->config;
        new_volume->modifier    = volume->modifier;
        new_volume->material_id(volume->material_id());
        
        new_objects->push_back(new_object);
        delete *mesh;
    }
    
    return;
}

#ifdef SLIC3RXS
REGISTER_CLASS(ModelObject, "Model::Object");
#endif


ModelVolume::ModelVolume(ModelObject* object, const TriangleMesh &mesh)
:   object(object), mesh(mesh), modifier(false)
{}

ModelVolume::ModelVolume(ModelObject* object, const ModelVolume &other)
:   object(object), name(other.name), mesh(other.mesh), config(other.config),
    modifier(other.modifier)
{
    this->material_id(other.material_id());
}

t_model_material_id
ModelVolume::material_id() const
{
    return this->_material_id;
}

void
ModelVolume::material_id(t_model_material_id material_id)
{
    this->_material_id = material_id;
    
    // ensure this->_material_id references an existing material
    (void)this->object->get_model()->add_material(material_id);
}

ModelMaterial*
ModelVolume::material() const
{
    return this->object->get_model()->get_material(this->_material_id);
}

void
ModelVolume::set_material(t_model_material_id material_id, const ModelMaterial &material)
{
    this->_material_id = material_id;
    (void)this->object->get_model()->add_material(material_id, material);
}

ModelMaterial*
ModelVolume::assign_unique_material()
{
    Model* model = this->get_object()->get_model();
    
    this->_material_id = 1 + model->materials.size();
    return model->add_material(this->_material_id);
}

#ifdef SLIC3RXS
REGISTER_CLASS(ModelVolume, "Model::Volume");
#endif


ModelInstance::ModelInstance(ModelObject *object)
:   object(object), rotation(0), scaling_factor(1)
{}

ModelInstance::ModelInstance(ModelObject *object, const ModelInstance &other)
:   object(object), rotation(other.rotation), scaling_factor(other.scaling_factor), offset(other.offset)
{}

void
ModelInstance::transform_mesh(TriangleMesh* mesh, bool dont_translate) const
{
    mesh->rotate_z(this->rotation);                 // rotate around mesh origin
    mesh->scale(this->scaling_factor);              // scale around mesh origin
    if (!dont_translate)
        mesh->translate(this->offset.x, this->offset.y, 0);
}

void
ModelInstance::transform_polygon(Polygon* polygon) const
{
    polygon->rotate(this->rotation, Point(0,0));    // rotate around polygon origin
    polygon->scale(this->scaling_factor);           // scale around polygon origin
}

#ifdef SLIC3RXS
REGISTER_CLASS(ModelInstance, "Model::Instance");
#endif

}
