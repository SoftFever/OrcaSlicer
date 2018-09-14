#include "Model.hpp"
#include "Geometry.hpp"

#include "Format/AMF.hpp"
#include "Format/OBJ.hpp"
#include "Format/PRUS.hpp"
#include "Format/STL.hpp"
#include "Format/3mf.hpp"

#include <float.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "SVG.hpp"
#include <Eigen/Dense>

static const float UNIT_MATRIX[] = { 1.0f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 1.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f, 1.0f };

namespace Slic3r {

    unsigned int Model::s_auto_extruder_id = 1;

Model::Model(const Model &other)
{
    // copy materials
    for (const auto &m : other.materials)
        this->add_material(m.first, *m.second);
    // copy objects
    this->objects.reserve(other.objects.size());
    for (const ModelObject *o : other.objects)
        this->add_object(*o, true);
}

Model& Model::operator=(Model other)
{
    this->swap(other);
    return *this;
}

void Model::swap(Model &other)
{
    std::swap(this->materials,  other.materials);
    std::swap(this->objects,    other.objects);
}

Model Model::read_from_file(const std::string &input_file, bool add_default_instances)
{
    Model model;
    
    bool result = false;
    if (boost::algorithm::iends_with(input_file, ".stl"))
        result = load_stl(input_file.c_str(), &model);
    else if (boost::algorithm::iends_with(input_file, ".obj"))
        result = load_obj(input_file.c_str(), &model);
    else if (!boost::algorithm::iends_with(input_file, ".zip.amf") && (boost::algorithm::iends_with(input_file, ".amf") ||
        boost::algorithm::iends_with(input_file, ".amf.xml")))
        result = load_amf(input_file.c_str(), nullptr, &model);
#ifdef SLIC3R_PRUS
    else if (boost::algorithm::iends_with(input_file, ".prusa"))
        result = load_prus(input_file.c_str(), &model);
#endif /* SLIC3R_PRUS */
    else
        throw std::runtime_error("Unknown file format. Input file must have .stl, .obj, .amf(.xml) or .prusa extension.");

    if (! result)
        throw std::runtime_error("Loading of a model file failed.");

    if (model.objects.empty())
        throw std::runtime_error("The supplied file couldn't be read because it's empty");
    
    for (ModelObject *o : model.objects)
        o->input_file = input_file;
    
    if (add_default_instances)
        model.add_default_instances();

    return model;
}

Model Model::read_from_archive(const std::string &input_file, PresetBundle* bundle, bool add_default_instances)
{
    Model model;

    bool result = false;
    if (boost::algorithm::iends_with(input_file, ".3mf"))
        result = load_3mf(input_file.c_str(), bundle, &model);
    else if (boost::algorithm::iends_with(input_file, ".zip.amf"))
        result = load_amf(input_file.c_str(), bundle, &model);
    else
        throw std::runtime_error("Unknown file format. Input file must have .3mf or .zip.amf extension.");

    if (!result)
        throw std::runtime_error("Loading of a model file failed.");

    if (model.objects.empty())
        throw std::runtime_error("The supplied file couldn't be read because it's empty");

    for (ModelObject *o : model.objects)
    {
        if (boost::algorithm::iends_with(input_file, ".zip.amf"))
        {
            // we remove the .zip part of the extension to avoid it be added to filenames when exporting
            o->input_file = boost::ireplace_last_copy(input_file, ".zip.", ".");
        }
        else
            o->input_file = input_file;
    }

    if (add_default_instances)
        model.add_default_instances();

    return model;
}

ModelObject* Model::add_object()
{
    this->objects.emplace_back(new ModelObject(this));
    return this->objects.back();
}

ModelObject* Model::add_object(const char *name, const char *path, const TriangleMesh &mesh)
{
    ModelObject* new_object = new ModelObject(this);
    this->objects.push_back(new_object);
    new_object->name = name;
    new_object->input_file = path;
    ModelVolume *new_volume = new_object->add_volume(mesh);
    new_volume->name = name;
    new_object->invalidate_bounding_box();
    return new_object;
}

ModelObject* Model::add_object(const char *name, const char *path, TriangleMesh &&mesh)
{
    ModelObject* new_object = new ModelObject(this);
    this->objects.push_back(new_object);
    new_object->name = name;
    new_object->input_file = path;
    ModelVolume *new_volume = new_object->add_volume(std::move(mesh));
    new_volume->name = name;
    new_object->invalidate_bounding_box();
    return new_object;
}

ModelObject* Model::add_object(const ModelObject &other, bool copy_volumes)
{
    ModelObject* new_object = new ModelObject(this, other, copy_volumes);
    this->objects.push_back(new_object);
    return new_object;
}

void Model::delete_object(size_t idx)
{
    ModelObjectPtrs::iterator i = this->objects.begin() + idx;
    delete *i;
    this->objects.erase(i);
}

void Model::delete_object(ModelObject* object)
{
    if (object == nullptr)
        return;

    for (ModelObjectPtrs::iterator it = objects.begin(); it != objects.end(); ++it)
    {
        ModelObject* obj = *it;
        if (obj == object)
        {
            delete obj;
            objects.erase(it);
            return;
        }
    }
}

void Model::clear_objects()
{
    for (ModelObject *o : this->objects)
        delete o;
    this->objects.clear();
}

void Model::delete_material(t_model_material_id material_id)
{
    ModelMaterialMap::iterator i = this->materials.find(material_id);
    if (i != this->materials.end()) {
        delete i->second;
        this->materials.erase(i);
    }
}

void Model::clear_materials()
{
    for (auto &m : this->materials)
        delete m.second;
    this->materials.clear();
}

ModelMaterial* Model::add_material(t_model_material_id material_id)
{
    ModelMaterial* material = this->get_material(material_id);
    if (material == nullptr)
        material = this->materials[material_id] = new ModelMaterial(this);
    return material;
}

ModelMaterial* Model::add_material(t_model_material_id material_id, const ModelMaterial &other)
{
    // delete existing material if any
    ModelMaterial* material = this->get_material(material_id);
    delete material;
    // set new material
    material = new ModelMaterial(this, other);
    this->materials[material_id] = material;
    return material;
}

// makes sure all objects have at least one instance
bool Model::add_default_instances()
{
    // apply a default position to all objects not having one
    for (ModelObject *o : this->objects)
        if (o->instances.empty())
            o->add_instance();
    return true;
}

// this returns the bounding box of the *transformed* instances
BoundingBoxf3 Model::bounding_box() const
{
    BoundingBoxf3 bb;
    for (ModelObject *o : this->objects)
        bb.merge(o->bounding_box());
    return bb;
}

void Model::center_instances_around_point(const Pointf &point)
{
//    BoundingBoxf3 bb = this->bounding_box();
    BoundingBoxf3 bb;
    for (ModelObject *o : this->objects)
        for (size_t i = 0; i < o->instances.size(); ++ i)
            bb.merge(o->instance_bounding_box(i, false));

    Sizef3 size = bb.size();
    coordf_t shift_x = -bb.min.x + point.x - size.x/2;
    coordf_t shift_y = -bb.min.y + point.y - size.y/2;
    for (ModelObject *o : this->objects) {
        for (ModelInstance *i : o->instances)
            i->offset.translate(shift_x, shift_y);
        o->invalidate_bounding_box();
    }
}

// flattens everything to a single mesh
TriangleMesh Model::mesh() const
{
    TriangleMesh mesh;
    for (const ModelObject *o : this->objects)
        mesh.merge(o->mesh());
    return mesh;
}

static bool _arrange(const Pointfs &sizes, coordf_t dist, const BoundingBoxf* bb, Pointfs &out)
{
    if (sizes.empty())
        // return if the list is empty or the following call to BoundingBoxf constructor will lead to a crash
        return true;

    // we supply unscaled data to arrange()
    bool result = Slic3r::Geometry::arrange(
        sizes.size(),               // number of parts
        BoundingBoxf(sizes).max,    // width and height of a single cell
        dist,                       // distance between cells
        bb,                         // bounding box of the area to fill
        out                         // output positions
    );

    if (!result && bb != nullptr) {
        // Try to arrange again ignoring bb
        result = Slic3r::Geometry::arrange(
            sizes.size(),               // number of parts
            BoundingBoxf(sizes).max,    // width and height of a single cell
            dist,                       // distance between cells
            nullptr,                    // bounding box of the area to fill
            out                         // output positions
        );
    }
    
    return result;
}

/*  arrange objects preserving their instance count
    but altering their instance positions */
bool Model::arrange_objects(coordf_t dist, const BoundingBoxf* bb)
{
    // get the (transformed) size of each instance so that we take
    // into account their different transformations when packing
    Pointfs instance_sizes;
    Pointfs instance_centers;
    for (const ModelObject *o : this->objects)
        for (size_t i = 0; i < o->instances.size(); ++ i) {
            // an accurate snug bounding box around the transformed mesh.
            BoundingBoxf3 bbox(o->instance_bounding_box(i, true));
            instance_sizes.push_back(bbox.size());
            instance_centers.push_back(bbox.center());
        }

    Pointfs positions;
    if (! _arrange(instance_sizes, dist, bb, positions))
        return false;

    size_t idx = 0;
    for (ModelObject *o : this->objects) {
        for (ModelInstance *i : o->instances) {
            i->offset = positions[idx] - instance_centers[idx];
            ++ idx;
        }
        o->invalidate_bounding_box();
    }

    return true;
}

// Duplicate the entire model preserving instance relative positions.
void Model::duplicate(size_t copies_num, coordf_t dist, const BoundingBoxf* bb)
{
    Pointfs model_sizes(copies_num-1, this->bounding_box().size());
    Pointfs positions;
    if (! _arrange(model_sizes, dist, bb, positions))
        CONFESS("Cannot duplicate part as the resulting objects would not fit on the print bed.\n");
    
    // note that this will leave the object count unaltered
    
    for (ModelObject *o : this->objects) {
        // make a copy of the pointers in order to avoid recursion when appending their copies
        ModelInstancePtrs instances = o->instances;
        for (const ModelInstance *i : instances) {
            for (const Pointf &pos : positions) {
                ModelInstance *instance = o->add_instance(*i);
                instance->offset.translate(pos);
            }
        }
        o->invalidate_bounding_box();
    }
}

/*  this will append more instances to each object
    and then automatically rearrange everything */
void Model::duplicate_objects(size_t copies_num, coordf_t dist, const BoundingBoxf* bb)
{
    for (ModelObject *o : this->objects) {
        // make a copy of the pointers in order to avoid recursion when appending their copies
        ModelInstancePtrs instances = o->instances;
        for (const ModelInstance *i : instances)
            for (size_t k = 2; k <= copies_num; ++ k)
                o->add_instance(*i);
    }
    
    this->arrange_objects(dist, bb);
}

void Model::duplicate_objects_grid(size_t x, size_t y, coordf_t dist)
{
    if (this->objects.size() > 1) throw "Grid duplication is not supported with multiple objects";
    if (this->objects.empty()) throw "No objects!";

    ModelObject* object = this->objects.front();
    object->clear_instances();

    Sizef3 size = object->bounding_box().size();

    for (size_t x_copy = 1; x_copy <= x; ++x_copy) {
        for (size_t y_copy = 1; y_copy <= y; ++y_copy) {
            ModelInstance* instance = object->add_instance();
            instance->offset.x = (size.x + dist) * (x_copy-1);
            instance->offset.y = (size.y + dist) * (y_copy-1);
        }
    }
}

bool Model::looks_like_multipart_object() const
{
    if (this->objects.size() <= 1)
        return false;
    double zmin = std::numeric_limits<double>::max();
    for (const ModelObject *obj : this->objects) {
        if (obj->volumes.size() > 1 || obj->config.keys().size() > 1)
            return false;
        for (const ModelVolume *vol : obj->volumes) {
            double zmin_this = vol->mesh.bounding_box().min.z;
            if (zmin == std::numeric_limits<double>::max())
                zmin = zmin_this;
            else if (std::abs(zmin - zmin_this) > EPSILON)
                // The volumes don't share zmin.
                return true;
        }
    }
    return false;
}

void Model::convert_multipart_object(unsigned int max_extruders)
{
    if (this->objects.empty())
        return;
    
    ModelObject* object = new ModelObject(this);
    object->input_file = this->objects.front()->input_file;

    reset_auto_extruder_id();

    for (const ModelObject* o : this->objects)
        for (const ModelVolume* v : o->volumes)
        {
            ModelVolume* new_v = object->add_volume(*v);
            if (new_v != nullptr)
            {
                new_v->name = o->name;
                new_v->config.set_deserialize("extruder", get_auto_extruder_id_as_string(max_extruders));
            }
        }

    for (const ModelInstance* i : this->objects.front()->instances)
        object->add_instance(*i);
    
    this->clear_objects();
    this->objects.push_back(object);
}

void Model::adjust_min_z()
{
    if (objects.empty())
        return;

    if (bounding_box().min.z < 0.0)
    {
        for (ModelObject* obj : objects)
        {
            if (obj != nullptr)
            {
                coordf_t obj_min_z = obj->bounding_box().min.z;
                if (obj_min_z < 0.0)
                    obj->translate(0.0, 0.0, -obj_min_z);
            }
        }
    }
}

unsigned int Model::get_auto_extruder_id(unsigned int max_extruders)
{
    unsigned int id = s_auto_extruder_id;

    if (++s_auto_extruder_id > max_extruders)
        reset_auto_extruder_id();

    return id;
}

std::string Model::get_auto_extruder_id_as_string(unsigned int max_extruders)
{
    char str_extruder[64];
    sprintf(str_extruder, "%ud", get_auto_extruder_id(max_extruders));
    return str_extruder;
}

void Model::reset_auto_extruder_id()
{
    s_auto_extruder_id = 1;
}

ModelObject::ModelObject(Model *model, const ModelObject &other, bool copy_volumes) :  
    name(other.name),
    input_file(other.input_file),
    instances(),
    volumes(),
    config(other.config),
    layer_height_ranges(other.layer_height_ranges),
    layer_height_profile(other.layer_height_profile),
    layer_height_profile_valid(other.layer_height_profile_valid),
    origin_translation(other.origin_translation),
    m_bounding_box(other.m_bounding_box),
    m_bounding_box_valid(other.m_bounding_box_valid),
    m_model(model)
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

ModelObject& ModelObject::operator=(ModelObject other)
{
    this->swap(other);
    return *this;
}

void ModelObject::swap(ModelObject &other)
{
    std::swap(this->input_file,             other.input_file);
    std::swap(this->instances,              other.instances);
    std::swap(this->volumes,                other.volumes);
    std::swap(this->config,                 other.config);
    std::swap(this->layer_height_ranges,    other.layer_height_ranges);
    std::swap(this->layer_height_profile,   other.layer_height_profile);
    std::swap(this->layer_height_profile_valid,    other.layer_height_profile_valid);
    std::swap(this->origin_translation,     other.origin_translation);
    std::swap(m_bounding_box,               other.m_bounding_box);
    std::swap(m_bounding_box_valid,         other.m_bounding_box_valid);
}

ModelObject::~ModelObject()
{
    this->clear_volumes();
    this->clear_instances();
}

ModelVolume* ModelObject::add_volume(const TriangleMesh &mesh)
{
    ModelVolume* v = new ModelVolume(this, mesh);
    this->volumes.push_back(v);
    this->invalidate_bounding_box();
    return v;
}

ModelVolume* ModelObject::add_volume(TriangleMesh &&mesh)
{
    ModelVolume* v = new ModelVolume(this, std::move(mesh));
    this->volumes.push_back(v);
    this->invalidate_bounding_box();
    return v;
}

ModelVolume* ModelObject::add_volume(const ModelVolume &other)
{
    ModelVolume* v = new ModelVolume(this, other);
    this->volumes.push_back(v);
    this->invalidate_bounding_box();
    return v;
}

void ModelObject::delete_volume(size_t idx)
{
    ModelVolumePtrs::iterator i = this->volumes.begin() + idx;
    delete *i;
    this->volumes.erase(i);
    this->invalidate_bounding_box();
}

void ModelObject::clear_volumes()
{
    for (ModelVolume *v : this->volumes)
        delete v;
    this->volumes.clear();
    this->invalidate_bounding_box();
}

ModelInstance* ModelObject::add_instance()
{
    ModelInstance* i = new ModelInstance(this);
    this->instances.push_back(i);
    this->invalidate_bounding_box();
    return i;
}

ModelInstance* ModelObject::add_instance(const ModelInstance &other)
{
    ModelInstance* i = new ModelInstance(this, other);
    this->instances.push_back(i);
    this->invalidate_bounding_box();
    return i;
}

void ModelObject::delete_instance(size_t idx)
{
    ModelInstancePtrs::iterator i = this->instances.begin() + idx;
    delete *i;
    this->instances.erase(i);
    this->invalidate_bounding_box();
}

void ModelObject::delete_last_instance()
{
    this->delete_instance(this->instances.size() - 1);
}

void ModelObject::clear_instances()
{
    for (ModelInstance *i : this->instances)
        delete i;
    this->instances.clear();
    this->invalidate_bounding_box();
}

// Returns the bounding box of the transformed instances.
// This bounding box is approximate and not snug.
const BoundingBoxf3& ModelObject::bounding_box() const
{
    if (! m_bounding_box_valid) {
        BoundingBoxf3 raw_bbox;
        for (const ModelVolume *v : this->volumes)
            if (v->is_model_part())
                // mesh.bounding_box() returns a cached value.
                raw_bbox.merge(v->mesh.bounding_box());
        BoundingBoxf3 bb;
        for (const ModelInstance *i : this->instances)
            bb.merge(i->transform_bounding_box(raw_bbox));
        m_bounding_box = bb;
        m_bounding_box_valid = true;
    }
    return m_bounding_box;
}

// A mesh containing all transformed instances of this object.
TriangleMesh ModelObject::mesh() const
{
    TriangleMesh mesh;
    TriangleMesh raw_mesh = this->raw_mesh();
    for (const ModelInstance *i : this->instances) {
        TriangleMesh m = raw_mesh;
        i->transform_mesh(&m);
        mesh.merge(m);
    }
    return mesh;
}

// Non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
// Currently used by ModelObject::mesh(), to calculate the 2D envelope for 2D platter
// and to display the object statistics at ModelObject::print_info().
TriangleMesh ModelObject::raw_mesh() const
{
    TriangleMesh mesh;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part())
            mesh.merge(v->mesh);
    return mesh;
}

// A transformed snug bounding box around the non-modifier object volumes, without the translation applied.
// This bounding box is only used for the actual slicing.
BoundingBoxf3 ModelObject::raw_bounding_box() const
{
    BoundingBoxf3 bb;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            if (this->instances.empty()) CONFESS("Can't call raw_bounding_box() with no instances");
            bb.merge(this->instances.front()->transform_mesh_bounding_box(&v->mesh, true));
        }
    return bb;
}

// This returns an accurate snug bounding box of the transformed object instance, without the translation applied.
BoundingBoxf3 ModelObject::instance_bounding_box(size_t instance_idx, bool dont_translate) const
{
    BoundingBoxf3 bb;
    for (ModelVolume *v : this->volumes)
        if (v->is_model_part())
            bb.merge(this->instances[instance_idx]->transform_mesh_bounding_box(&v->mesh, dont_translate));
    return bb;
}

void ModelObject::center_around_origin()
{
    // calculate the displacements needed to 
    // center this object around the origin
	BoundingBoxf3 bb;
	for (ModelVolume *v : this->volumes)
        if (v->is_model_part())
			bb.merge(v->mesh.bounding_box());
    
    // first align to origin on XYZ
    Vectorf3 vector(-bb.min.x, -bb.min.y, -bb.min.z);
    
    // then center it on XY
    Sizef3 size = bb.size();
    vector.x -= size.x/2;
    vector.y -= size.y/2;
    
    this->translate(vector);
    this->origin_translation.translate(vector);
    
    if (!this->instances.empty()) {
        for (ModelInstance *i : this->instances) {
            // apply rotation and scaling to vector as well before translating instance,
            // in order to leave final position unaltered
            Vectorf3 v = vector.negative();
            v.rotate(i->rotation);
            v.scale(i->scaling_factor);
            i->offset.translate(v.x, v.y);
        }
        this->invalidate_bounding_box();
    }
}

void ModelObject::translate(coordf_t x, coordf_t y, coordf_t z)
{
    for (ModelVolume *v : this->volumes)
    {
        v->mesh.translate(float(x), float(y), float(z));
        v->m_convex_hull.translate(float(x), float(y), float(z));
    }

    if (m_bounding_box_valid)
        m_bounding_box.translate(x, y, z);
}

void ModelObject::scale(const Pointf3 &versor)
{
    for (ModelVolume *v : this->volumes)
    {
        v->mesh.scale(versor);
        v->m_convex_hull.scale(versor);
    }
    // reset origin translation since it doesn't make sense anymore
    this->origin_translation = Pointf3(0,0,0);
    this->invalidate_bounding_box();
}

void ModelObject::rotate(float angle, const Pointf3& axis)
{
    for (ModelVolume *v : this->volumes)
    {
        v->mesh.rotate(angle, axis);
        v->m_convex_hull.rotate(angle, axis);
    }

    center_around_origin();

    this->origin_translation = Pointf3(0, 0, 0);
    this->invalidate_bounding_box();
}

void ModelObject::transform(const float* matrix3x4)
{
    if (matrix3x4 == nullptr)
        return;

    for (ModelVolume* v : volumes)
    {
        v->mesh.transform(matrix3x4);
        v->m_convex_hull.transform(matrix3x4);
    }

    origin_translation = Pointf3(0.0, 0.0, 0.0);
    invalidate_bounding_box();
}

void ModelObject::mirror(const Axis &axis)
{
    for (ModelVolume *v : this->volumes)
    {
        v->mesh.mirror(axis);
        v->m_convex_hull.mirror(axis);
    }

    this->origin_translation = Pointf3(0, 0, 0);
    this->invalidate_bounding_box();
}

size_t ModelObject::materials_count() const
{
    std::set<t_model_material_id> material_ids;
    for (const ModelVolume *v : this->volumes)
        material_ids.insert(v->material_id());
    return material_ids.size();
}

size_t ModelObject::facets_count() const
{
    size_t num = 0;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part())
            num += v->mesh.stl.stats.number_of_facets;
    return num;
}

bool ModelObject::needed_repair() const
{
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part() && v->mesh.needed_repair())
            return true;
    return false;
}

void ModelObject::cut(coordf_t z, Model* model) const
{
    // clone this one to duplicate instances, materials etc.
    ModelObject* upper = model->add_object(*this);
    ModelObject* lower = model->add_object(*this);
    upper->clear_volumes();
    lower->clear_volumes();
    upper->input_file = "";
    lower->input_file = "";
    
    for (ModelVolume *volume : this->volumes) {
        if (! volume->is_model_part()) {
            // don't cut modifiers
            upper->add_volume(*volume);
            lower->add_volume(*volume);
        } else {
            TriangleMesh upper_mesh, lower_mesh;
            TriangleMeshSlicer tms(&volume->mesh);
            tms.cut(z, &upper_mesh, &lower_mesh);

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

void ModelObject::split(ModelObjectPtrs* new_objects)
{
    if (this->volumes.size() > 1) {
        // We can't split meshes if there's more than one volume, because
        // we can't group the resulting meshes by object afterwards
        new_objects->push_back(this);
        return;
    }
    
    ModelVolume* volume = this->volumes.front();
    TriangleMeshPtrs meshptrs = volume->mesh.split();
    for (TriangleMesh *mesh : meshptrs) {
        // Snap the mesh to Z=0.
        float z0 = FLT_MAX;
        
        mesh->repair();
        
        ModelObject* new_object = m_model->add_object(*this, false);
        new_object->input_file  = "";
        ModelVolume* new_volume = new_object->add_volume(*mesh);
        new_volume->name        = volume->name;
        new_volume->config      = volume->config;
        new_volume->set_type(volume->type());
        new_volume->material_id(volume->material_id());
        
        new_objects->push_back(new_object);
        delete mesh;
    }
    
    return;
}

void ModelObject::check_instances_print_volume_state(const BoundingBoxf3& print_volume)
{
    for (const ModelVolume* vol : this->volumes)
    {
        if (vol->is_model_part())
        {
            for (ModelInstance* inst : this->instances)
            {
                std::vector<float> world_mat(UNIT_MATRIX, std::end(UNIT_MATRIX));
                Eigen::Transform<float, 3, Eigen::Affine> m = Eigen::Transform<float, 3, Eigen::Affine>::Identity();
                m.translate(Eigen::Vector3f((float)inst->offset.x, (float)inst->offset.y, 0.0f));
                m.rotate(Eigen::AngleAxisf(inst->rotation, Eigen::Vector3f::UnitZ()));
                m.scale(inst->scaling_factor);
                ::memcpy((void*)world_mat.data(), (const void*)m.data(), 16 * sizeof(float));

                BoundingBoxf3 bb = vol->get_convex_hull().transformed_bounding_box(world_mat);

                if (print_volume.contains(bb))
                    inst->print_volume_state = ModelInstance::PVS_Inside;
                else if (print_volume.intersects(bb))
                    inst->print_volume_state = ModelInstance::PVS_Partly_Outside;
                else
                    inst->print_volume_state = ModelInstance::PVS_Fully_Outside;
            }
        }
    }
}

void ModelObject::print_info() const
{
    using namespace std;
    cout << fixed;
    boost::nowide::cout << "[" << boost::filesystem::path(this->input_file).filename().string() << "]" << endl;
    
    TriangleMesh mesh = this->raw_mesh();
    mesh.check_topology();
    BoundingBoxf3 bb = mesh.bounding_box();
    Sizef3 size = bb.size();
    cout << "size_x = " << size.x << endl;
    cout << "size_y = " << size.y << endl;
    cout << "size_z = " << size.z << endl;
    cout << "min_x = " << bb.min.x << endl;
    cout << "min_y = " << bb.min.y << endl;
    cout << "min_z = " << bb.min.z << endl;
    cout << "max_x = " << bb.max.x << endl;
    cout << "max_y = " << bb.max.y << endl;
    cout << "max_z = " << bb.max.z << endl;
    cout << "number_of_facets = " << mesh.stl.stats.number_of_facets  << endl;
    cout << "manifold = "   << (mesh.is_manifold() ? "yes" : "no") << endl;
    
    mesh.repair();  // this calculates number_of_parts
    if (mesh.needed_repair()) {
        mesh.repair();
        if (mesh.stl.stats.degenerate_facets > 0)
            cout << "degenerate_facets = "  << mesh.stl.stats.degenerate_facets << endl;
        if (mesh.stl.stats.edges_fixed > 0)
            cout << "edges_fixed = "        << mesh.stl.stats.edges_fixed       << endl;
        if (mesh.stl.stats.facets_removed > 0)
            cout << "facets_removed = "     << mesh.stl.stats.facets_removed    << endl;
        if (mesh.stl.stats.facets_added > 0)
            cout << "facets_added = "       << mesh.stl.stats.facets_added      << endl;
        if (mesh.stl.stats.facets_reversed > 0)
            cout << "facets_reversed = "    << mesh.stl.stats.facets_reversed   << endl;
        if (mesh.stl.stats.backwards_edges > 0)
            cout << "backwards_edges = "    << mesh.stl.stats.backwards_edges   << endl;
    }
    cout << "number_of_parts =  " << mesh.stl.stats.number_of_parts << endl;
    cout << "volume = "           << mesh.volume()                  << endl;
}

void ModelVolume::material_id(t_model_material_id material_id)
{
    this->_material_id = material_id;
    
    // ensure this->_material_id references an existing material
    (void)this->object->get_model()->add_material(material_id);
}

ModelMaterial* ModelVolume::material() const
{ 
    return this->object->get_model()->get_material(this->_material_id);
}

void ModelVolume::set_material(t_model_material_id material_id, const ModelMaterial &material)
{
    this->_material_id = material_id;
    (void)this->object->get_model()->add_material(material_id, material);
}

ModelMaterial* ModelVolume::assign_unique_material()
{
    Model* model = this->get_object()->get_model();
    
    // as material-id "0" is reserved by the AMF spec we start from 1
    this->_material_id = 1 + model->materials.size();  // watchout for implicit cast
    return model->add_material(this->_material_id);
}

void ModelVolume::calculate_convex_hull()
{
    m_convex_hull = mesh.convex_hull_3d();
}

const TriangleMesh& ModelVolume::get_convex_hull() const
{
    return m_convex_hull;
}

ModelVolume::Type ModelVolume::type_from_string(const std::string &s)
{
    // Legacy support
    if (s == "0")
        return MODEL_PART;
    if (s == "1")
        return PARAMETER_MODIFIER;
    // New type (supporting the support enforcers & blockers)
    if (s == "ModelPart")
        return MODEL_PART;
    if (s == "ParameterModifier")
        return PARAMETER_MODIFIER;
    if (s == "SupportEnforcer")
        return SUPPORT_ENFORCER;
    if (s == "SupportBlocker")
        return SUPPORT_BLOCKER;
}

std::string ModelVolume::type_to_string(const Type t)
{
    switch (t) {
    case MODEL_PART:         return "ModelPart";
    case PARAMETER_MODIFIER: return "ParameterModifier";
    case SUPPORT_ENFORCER:   return "SupportEnforcer";
    case SUPPORT_BLOCKER:    return "SupportBlocker";
    default:
        assert(false);
        return "ModelPart";
    }
}

// Split this volume, append the result to the object owning this volume.
// Return the number of volumes created from this one.
// This is useful to assign different materials to different volumes of an object.
size_t ModelVolume::split(unsigned int max_extruders)
{
    TriangleMeshPtrs meshptrs = this->mesh.split();
    if (meshptrs.size() <= 1) {
        delete meshptrs.front();
        return 1;
    }

    size_t idx = 0;
    size_t ivolume = std::find(this->object->volumes.begin(), this->object->volumes.end(), this) - this->object->volumes.begin();
    std::string name = this->name;

    Model::reset_auto_extruder_id();

    for (TriangleMesh *mesh : meshptrs) {
        mesh->repair();
        if (idx == 0)
            this->mesh = std::move(*mesh);
        else
            this->object->volumes.insert(this->object->volumes.begin() + (++ ivolume), new ModelVolume(object, *this, std::move(*mesh)));
        char str_idx[64];
        sprintf(str_idx, "_%d", idx + 1);
        this->object->volumes[ivolume]->name = name + str_idx;
        this->object->volumes[ivolume]->config.set_deserialize("extruder", Model::get_auto_extruder_id_as_string(max_extruders));
        delete mesh;
        ++ idx;
    }
    
    return idx;
}

void ModelInstance::transform_mesh(TriangleMesh* mesh, bool dont_translate) const
{
    mesh->rotate_z(this->rotation);                 // rotate around mesh origin
    mesh->scale(this->scaling_factor);              // scale around mesh origin
    if (!dont_translate)
        mesh->translate(this->offset.x, this->offset.y, 0);
}

BoundingBoxf3 ModelInstance::transform_mesh_bounding_box(const TriangleMesh* mesh, bool dont_translate) const
{
    // Rotate around mesh origin.
    double c = cos(this->rotation);
    double s = sin(this->rotation);
    BoundingBoxf3 bbox;
    for (int i = 0; i < mesh->stl.stats.number_of_facets; ++ i) {
        const stl_facet &facet = mesh->stl.facet_start[i];
        for (int j = 0; j < 3; ++ j) {
            stl_vertex v = facet.vertex[j];
            double xold = v.x;
            double yold = v.y;
            v.x = float(c * xold - s * yold);
            v.y = float(s * xold + c * yold);
            bbox.merge(Pointf3(v.x, v.y, v.z));
        }
    }
    if (! empty(bbox)) {
        // Scale the bounding box uniformly.
        if (std::abs(this->scaling_factor - 1.) > EPSILON) {
            bbox.min.x *= float(this->scaling_factor);
            bbox.min.y *= float(this->scaling_factor);
            bbox.min.z *= float(this->scaling_factor);
            bbox.max.x *= float(this->scaling_factor);
            bbox.max.y *= float(this->scaling_factor);
            bbox.max.z *= float(this->scaling_factor);
        }
        // Translate the bounding box.
        if (! dont_translate) {
            bbox.min.x += float(this->offset.x);
            bbox.min.y += float(this->offset.y);
            bbox.max.x += float(this->offset.x);
            bbox.max.y += float(this->offset.y);
        }
    }
    return bbox;
}

BoundingBoxf3 ModelInstance::transform_bounding_box(const BoundingBoxf3 &bbox, bool dont_translate) const
{
    Eigen::Transform<float, 3, Eigen::Affine> matrix = Eigen::Transform<float, 3, Eigen::Affine>::Identity();
    if (!dont_translate)
        matrix.translate(Eigen::Vector3f((float)offset.x, (float)offset.y, 0.0f));

    matrix.rotate(Eigen::AngleAxisf(rotation, Eigen::Vector3f::UnitZ()));
    matrix.scale(scaling_factor);

    std::vector<float> m(16, 0.0f);
    ::memcpy((void*)m.data(), (const void*)matrix.data(), 16 * sizeof(float));
    return bbox.transformed(m);
}

void ModelInstance::transform_polygon(Polygon* polygon) const
{
    polygon->rotate(this->rotation);                // rotate around polygon origin
    polygon->scale(this->scaling_factor);           // scale around polygon origin
}

}
