#include "libslic3r.h"
#include "Exception.hpp"
#include "Model.hpp"
#include "ModelArrange.hpp"
#include "Geometry.hpp"
#include "MTUtils.hpp"
#include "TriangleMeshSlicer.hpp"
#include "TriangleSelector.hpp"

#include "Format/AMF.hpp"
#include "Format/OBJ.hpp"
#include "Format/PRUS.hpp"
#include "Format/STL.hpp"
#include "Format/3mf.hpp"

#include <float.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/iostream.hpp>

#include "SVG.hpp"
#include <Eigen/Dense>
#include "GCodeWriter.hpp"

namespace Slic3r {

Model& Model::assign_copy(const Model &rhs)
{
    this->copy_id(rhs);
    // copy materials
    this->clear_materials();
    this->materials = rhs.materials;
    for (std::pair<const t_model_material_id, ModelMaterial*> &m : this->materials) {
        // Copy including the ID and m_model.
        m.second = new ModelMaterial(*m.second);
        m.second->set_model(this);
    }
    // copy objects
    this->clear_objects();
    this->objects.reserve(rhs.objects.size());
	for (const ModelObject *model_object : rhs.objects) {
        // Copy including the ID, leave ID set to invalid (zero).
        auto mo = ModelObject::new_copy(*model_object);
        mo->set_model(this);
		this->objects.emplace_back(mo);
    }

    // copy custom code per height
    this->custom_gcode_per_print_z = rhs.custom_gcode_per_print_z;
    return *this;
}

Model& Model::assign_copy(Model &&rhs)
{
    this->copy_id(rhs);
	// Move materials, adjust the parent pointer.
    this->clear_materials();
    this->materials = std::move(rhs.materials);
    for (std::pair<const t_model_material_id, ModelMaterial*> &m : this->materials)
        m.second->set_model(this);
    rhs.materials.clear();
    // Move objects, adjust the parent pointer.
    this->clear_objects();
	this->objects = std::move(rhs.objects);
    for (ModelObject *model_object : this->objects)
        model_object->set_model(this);
    rhs.objects.clear();

    // copy custom code per height
    this->custom_gcode_per_print_z = std::move(rhs.custom_gcode_per_print_z);
    return *this;
}

void Model::assign_new_unique_ids_recursive()
{
    this->set_new_unique_id();
    for (std::pair<const t_model_material_id, ModelMaterial*> &m : this->materials)
        m.second->assign_new_unique_ids_recursive();
    for (ModelObject *model_object : this->objects)
        model_object->assign_new_unique_ids_recursive();
}

void Model::update_links_bottom_up_recursive()
{
	for (std::pair<const t_model_material_id, ModelMaterial*> &kvp : this->materials)
		kvp.second->set_model(this);
	for (ModelObject *model_object : this->objects) {
		model_object->set_model(this);
		for (ModelInstance *model_instance : model_object->instances)
			model_instance->set_model_object(model_object);
		for (ModelVolume *model_volume : model_object->volumes)
			model_volume->set_model_object(model_object);
	}
}

Model Model::read_from_file(const std::string& input_file, DynamicPrintConfig* config, bool add_default_instances, bool check_version)
{
    Model model;

    DynamicPrintConfig temp_config;
    if (config == nullptr)
        config = &temp_config;

    bool result = false;
    if (boost::algorithm::iends_with(input_file, ".stl"))
        result = load_stl(input_file.c_str(), &model);
    else if (boost::algorithm::iends_with(input_file, ".obj"))
        result = load_obj(input_file.c_str(), &model);
    else if (boost::algorithm::iends_with(input_file, ".amf") || boost::algorithm::iends_with(input_file, ".amf.xml"))
        result = load_amf(input_file.c_str(), config, &model, check_version);
    else if (boost::algorithm::iends_with(input_file, ".3mf"))
        result = load_3mf(input_file.c_str(), config, &model, false);
    else if (boost::algorithm::iends_with(input_file, ".prusa"))
        result = load_prus(input_file.c_str(), &model);
    else
        throw Slic3r::RuntimeError("Unknown file format. Input file must have .stl, .obj, .amf(.xml) or .prusa extension.");

    if (! result)
        throw Slic3r::RuntimeError("Loading of a model file failed.");

    if (model.objects.empty())
        throw Slic3r::RuntimeError("The supplied file couldn't be read because it's empty");
    
    for (ModelObject *o : model.objects)
        o->input_file = input_file;
    
    if (add_default_instances)
        model.add_default_instances();

    CustomGCode::update_custom_gcode_per_print_z_from_config(model.custom_gcode_per_print_z, config);
    CustomGCode::check_mode_for_custom_gcode_per_print_z(model.custom_gcode_per_print_z);

    return model;
}

Model Model::read_from_archive(const std::string& input_file, DynamicPrintConfig* config, bool add_default_instances, bool check_version)
{
    Model model;

    bool result = false;
    if (boost::algorithm::iends_with(input_file, ".3mf"))
        result = load_3mf(input_file.c_str(), config, &model, check_version);
    else if (boost::algorithm::iends_with(input_file, ".zip.amf"))
        result = load_amf(input_file.c_str(), config, &model, check_version);
    else
        throw Slic3r::RuntimeError("Unknown file format. Input file must have .3mf or .zip.amf extension.");

    if (!result)
        throw Slic3r::RuntimeError("Loading of a model file failed.");

    if (model.objects.empty())
        throw Slic3r::RuntimeError("The supplied file couldn't be read because it's empty");

    for (ModelObject *o : model.objects)
    {
//        if (boost::algorithm::iends_with(input_file, ".zip.amf"))
//        {
//            // we remove the .zip part of the extension to avoid it be added to filenames when exporting
//            o->input_file = boost::ireplace_last_copy(input_file, ".zip.", ".");
//        }
//        else
            o->input_file = input_file;
    }

    if (add_default_instances)
        model.add_default_instances();

    CustomGCode::update_custom_gcode_per_print_z_from_config(model.custom_gcode_per_print_z, config);
    CustomGCode::check_mode_for_custom_gcode_per_print_z(model.custom_gcode_per_print_z);

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
    new_volume->source.input_file = path;
    new_volume->source.object_idx = (int)this->objects.size() - 1;
    new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
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
    new_volume->source.input_file = path;
    new_volume->source.object_idx = (int)this->objects.size() - 1;
    new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
    new_object->invalidate_bounding_box();
    return new_object;
}

ModelObject* Model::add_object(const ModelObject &other)
{
	ModelObject* new_object = ModelObject::new_clone(other);
    new_object->set_model(this);
    this->objects.push_back(new_object);
    return new_object;
}

void Model::delete_object(size_t idx)
{
    ModelObjectPtrs::iterator i = this->objects.begin() + idx;
    delete *i;
    this->objects.erase(i);
}

bool Model::delete_object(ModelObject* object)
{
    if (object != nullptr) {
        size_t idx = 0;
        for (ModelObject *model_object : objects) {
            if (model_object == object) {
                delete model_object;
                objects.erase(objects.begin() + idx);
                return true;
            }
            ++ idx;
        }
    }
    return false;
}

bool Model::delete_object(ObjectID id)
{
    if (id.id != 0) {
        size_t idx = 0;
        for (ModelObject *model_object : objects) {
            if (model_object->id() == id) {
                delete model_object;
                objects.erase(objects.begin() + idx);
                return true;
            }
            ++ idx;
        }
    }
    return false;
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
    assert(! material_id.empty());
    ModelMaterial* material = this->get_material(material_id);
    if (material == nullptr)
        material = this->materials[material_id] = new ModelMaterial(this);
    return material;
}

ModelMaterial* Model::add_material(t_model_material_id material_id, const ModelMaterial &other)
{
    assert(! material_id.empty());
    // delete existing material if any
    ModelMaterial* material = this->get_material(material_id);
    delete material;
    // set new material
	material = new ModelMaterial(other);
	material->set_model(this);
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

unsigned int Model::update_print_volume_state(const BoundingBoxf3 &print_volume) 
{
    unsigned int num_printable = 0;
    for (ModelObject *model_object : this->objects)
        num_printable += model_object->check_instances_print_volume_state(print_volume);
    return num_printable;
}

bool Model::center_instances_around_point(const Vec2d &point)
{
    BoundingBoxf3 bb;
    for (ModelObject *o : this->objects)
        for (size_t i = 0; i < o->instances.size(); ++ i)
            bb.merge(o->instance_bounding_box(i, false));

    Vec2d shift2 = point - to_2d(bb.center());
	if (std::abs(shift2(0)) < EPSILON && std::abs(shift2(1)) < EPSILON)
		// No significant shift, don't do anything.
		return false;

	Vec3d shift3 = Vec3d(shift2(0), shift2(1), 0.0);
	for (ModelObject *o : this->objects) {
		for (ModelInstance *i : o->instances)
			i->set_offset(i->get_offset() + shift3);
		o->invalidate_bounding_box();
	}
	return true;
}

// flattens everything to a single mesh
TriangleMesh Model::mesh() const
{
    TriangleMesh mesh;
    for (const ModelObject *o : this->objects)
        mesh.merge(o->mesh());
    return mesh;
}

void Model::duplicate_objects_grid(size_t x, size_t y, coordf_t dist)
{
    if (this->objects.size() > 1) throw "Grid duplication is not supported with multiple objects";
    if (this->objects.empty()) throw "No objects!";

    ModelObject* object = this->objects.front();
    object->clear_instances();

    Vec3d ext_size = object->bounding_box().size() + dist * Vec3d::Ones();

    for (size_t x_copy = 1; x_copy <= x; ++x_copy) {
        for (size_t y_copy = 1; y_copy <= y; ++y_copy) {
            ModelInstance* instance = object->add_instance();
            instance->set_offset(Vec3d(ext_size(0) * (double)(x_copy - 1), ext_size(1) * (double)(y_copy - 1), 0.0));
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
            double zmin_this = vol->mesh().bounding_box().min(2);
            if (zmin == std::numeric_limits<double>::max())
                zmin = zmin_this;
            else if (std::abs(zmin - zmin_this) > EPSILON)
                // The volumes don't share zmin.
                return true;
        }
    }
    return false;
}

// Generate next extruder ID string, in the range of (1, max_extruders).
static inline std::string auto_extruder_id(unsigned int max_extruders, unsigned int &cntr)
{
    char str_extruder[64];
    sprintf(str_extruder, "%ud", cntr + 1);
    if (++ cntr == max_extruders)
    	cntr = 0;
    return str_extruder;
}

void Model::convert_multipart_object(unsigned int max_extruders)
{
	assert(this->objects.size() >= 2);
    if (this->objects.size() < 2)
        return;
    
    ModelObject* object = new ModelObject(this);
    object->input_file = this->objects.front()->input_file;
    object->name = this->objects.front()->name;
    //FIXME copy the config etc?

    unsigned int extruder_counter = 0;
	for (const ModelObject* o : this->objects)
    	for (const ModelVolume* v : o->volumes) {
            // If there are more than one object, put all volumes together 
            // Each object may contain any number of volumes and instances
            // The volumes transformations are relative to the object containing them...
            Geometry::Transformation trafo_volume = v->get_transformation();
            // Revert the centering operation.
            trafo_volume.set_offset(trafo_volume.get_offset() - o->origin_translation);
            int counter = 1;
            auto copy_volume = [o, max_extruders, &counter, &extruder_counter](ModelVolume *new_v) {
                assert(new_v != nullptr);
                new_v->name = o->name + "_" + std::to_string(counter++);
                new_v->config.set_deserialize("extruder", auto_extruder_id(max_extruders, extruder_counter));
                return new_v;
            };
            if (o->instances.empty()) {
            	copy_volume(object->add_volume(*v))->set_transformation(trafo_volume);
            } else {
            	for (const ModelInstance* i : o->instances)
                    // ...so, transform everything to a common reference system (world)
                	copy_volume(object->add_volume(*v))->set_transformation(i->get_transformation() * trafo_volume);                    
            }
        }

    // commented-out to fix #2868
//    object->add_instance();
//    object->instances[0]->set_offset(object->raw_mesh_bounding_box().center());

    this->clear_objects();
    this->objects.push_back(object);
}

bool Model::looks_like_imperial_units() const
{
    if (this->objects.size() == 0)
        return false;

    for (ModelObject* obj : this->objects)
        if (obj->get_object_stl_stats().volume < 9.0) // 9 = 3*3*3;
            return true;

    return false;
}

void Model::convert_from_imperial_units(bool only_small_volumes)
{
    double in_to_mm = 25.4;
    for (ModelObject* obj : this->objects)
        if (! only_small_volumes || obj->get_object_stl_stats().volume < 9.0) { // 9 = 3*3*3;
            obj->scale_mesh_after_creation(Vec3d(in_to_mm, in_to_mm, in_to_mm));
            for (ModelVolume* v : obj->volumes)
                v->source.is_converted_from_inches = true;
        }
}

bool Model::looks_like_saved_in_meters() const
{
    if (this->objects.size() == 0)
        return false;

    for (ModelObject* obj : this->objects)
        if (obj->get_object_stl_stats().volume < 0.001) // 0.001 = 0.1*0.1*0.1;
            return true;

    return false;
}

void Model::convert_from_meters(bool only_small_volumes)
{
    double m_to_mm = 1000;
    for (ModelObject* obj : this->objects)
        if (! only_small_volumes || obj->get_object_stl_stats().volume < 0.001) { // 0.001 = 0.1*0.1*0.1;
            obj->scale_mesh_after_creation(Vec3d(m_to_mm, m_to_mm, m_to_mm));
            for (ModelVolume* v : obj->volumes)
                v->source.is_converted_from_meters = true;
        }
}

void Model::adjust_min_z()
{
    if (objects.empty())
        return;

    if (bounding_box().min(2) < 0.0)
    {
        for (ModelObject* obj : objects)
        {
            if (obj != nullptr)
            {
                coordf_t obj_min_z = obj->bounding_box().min(2);
                if (obj_min_z < 0.0)
                    obj->translate_instances(Vec3d(0.0, 0.0, -obj_min_z));
            }
        }
    }
}

// Propose a filename including path derived from the ModelObject's input path.
// If object's name is filled in, use the object name, otherwise use the input name.
std::string Model::propose_export_file_name_and_path() const
{
    std::string input_file;
    for (const ModelObject *model_object : this->objects)
        for (ModelInstance *model_instance : model_object->instances)
            if (model_instance->is_printable()) {
                input_file = model_object->get_export_filename();

                if (!input_file.empty())
                    goto end;
                // Other instances will produce the same name, skip them.
                break;
            }
end:
    return input_file;
}

std::string Model::propose_export_file_name_and_path(const std::string &new_extension) const
{
    return boost::filesystem::path(this->propose_export_file_name_and_path()).replace_extension(new_extension).string();
}

ModelObject::~ModelObject()
{
    this->clear_volumes();
    this->clear_instances();
}

// maintains the m_model pointer
ModelObject& ModelObject::assign_copy(const ModelObject &rhs)
{
	assert(this->id().invalid() || this->id() == rhs.id());
	assert(this->config.id().invalid() || this->config.id() == rhs.config.id());
	this->copy_id(rhs);

    this->name                        = rhs.name;
    this->input_file                  = rhs.input_file;
    // Copies the config's ID
    this->config                      = rhs.config;
    assert(this->config.id() == rhs.config.id());
    this->sla_support_points          = rhs.sla_support_points;
    this->sla_points_status           = rhs.sla_points_status;
    this->sla_drain_holes             = rhs.sla_drain_holes;
    this->layer_config_ranges         = rhs.layer_config_ranges;
    this->layer_height_profile        = rhs.layer_height_profile;
    this->printable                   = rhs.printable;
    this->origin_translation          = rhs.origin_translation;
    m_bounding_box                    = rhs.m_bounding_box;
    m_bounding_box_valid              = rhs.m_bounding_box_valid;
    m_raw_bounding_box                = rhs.m_raw_bounding_box;
    m_raw_bounding_box_valid          = rhs.m_raw_bounding_box_valid;
    m_raw_mesh_bounding_box           = rhs.m_raw_mesh_bounding_box;
    m_raw_mesh_bounding_box_valid     = rhs.m_raw_mesh_bounding_box_valid;

    this->clear_volumes();
    this->volumes.reserve(rhs.volumes.size());
    for (ModelVolume *model_volume : rhs.volumes) {
        this->volumes.emplace_back(new ModelVolume(*model_volume));
        this->volumes.back()->set_model_object(this);
    }
    this->clear_instances();
	this->instances.reserve(rhs.instances.size());
    for (const ModelInstance *model_instance : rhs.instances) {
        this->instances.emplace_back(new ModelInstance(*model_instance));
        this->instances.back()->set_model_object(this);
    }

    return *this;
}

// maintains the m_model pointer
ModelObject& ModelObject::assign_copy(ModelObject &&rhs)
{
	assert(this->id().invalid());
    this->copy_id(rhs);

    this->name                        = std::move(rhs.name);
    this->input_file                  = std::move(rhs.input_file);
    // Moves the config's ID
    this->config                      = std::move(rhs.config);
    assert(this->config.id() == rhs.config.id());
    this->sla_support_points          = std::move(rhs.sla_support_points);
    this->sla_points_status           = std::move(rhs.sla_points_status);
    this->sla_drain_holes             = std::move(rhs.sla_drain_holes);
    this->layer_config_ranges         = std::move(rhs.layer_config_ranges);
    this->layer_height_profile        = std::move(rhs.layer_height_profile);
    this->origin_translation          = std::move(rhs.origin_translation);
    m_bounding_box                    = std::move(rhs.m_bounding_box);
    m_bounding_box_valid              = std::move(rhs.m_bounding_box_valid);
    m_raw_bounding_box                = rhs.m_raw_bounding_box;
    m_raw_bounding_box_valid          = rhs.m_raw_bounding_box_valid;
    m_raw_mesh_bounding_box           = rhs.m_raw_mesh_bounding_box;
    m_raw_mesh_bounding_box_valid     = rhs.m_raw_mesh_bounding_box_valid;

    this->clear_volumes();
	this->volumes = std::move(rhs.volumes);
	rhs.volumes.clear();
    for (ModelVolume *model_volume : this->volumes)
        model_volume->set_model_object(this);
    this->clear_instances();
	this->instances = std::move(rhs.instances);
	rhs.instances.clear();
    for (ModelInstance *model_instance : this->instances)
        model_instance->set_model_object(this);

    return *this;
}

void ModelObject::assign_new_unique_ids_recursive()
{
    this->set_new_unique_id();
    for (ModelVolume *model_volume : this->volumes)
        model_volume->assign_new_unique_ids_recursive();
    for (ModelInstance *model_instance : this->instances)
        model_instance->assign_new_unique_ids_recursive();
    this->layer_height_profile.set_new_unique_id();
}

// Clone this ModelObject including its volumes and instances, keep the IDs of the copies equal to the original.
// Called by Print::apply() to clone the Model / ModelObject hierarchy to the back end for background processing.
//ModelObject* ModelObject::clone(Model *parent)
//{
//    return new ModelObject(parent, *this, true);
//}

ModelVolume* ModelObject::add_volume(const TriangleMesh &mesh)
{
    ModelVolume* v = new ModelVolume(this, mesh);
    this->volumes.push_back(v);
    v->center_geometry_after_creation();
    this->invalidate_bounding_box();
    return v;
}

ModelVolume* ModelObject::add_volume(TriangleMesh &&mesh, ModelVolumeType type /*= ModelVolumeType::MODEL_PART*/)
{
    ModelVolume* v = new ModelVolume(this, std::move(mesh), type);
    this->volumes.push_back(v);
    v->center_geometry_after_creation();
    this->invalidate_bounding_box();
    return v;
}

ModelVolume* ModelObject::add_volume(const ModelVolume &other, ModelVolumeType type /*= ModelVolumeType::INVALID*/)
{
    ModelVolume* v = new ModelVolume(this, other);
    if (type != ModelVolumeType::INVALID && v->type() != type)
        v->set_type(type);
    this->volumes.push_back(v);
	// The volume should already be centered at this point of time when copying shared pointers of the triangle mesh and convex hull.
//	v->center_geometry_after_creation();
//    this->invalidate_bounding_box();
    return v;
}

ModelVolume* ModelObject::add_volume(const ModelVolume &other, TriangleMesh &&mesh)
{
    ModelVolume* v = new ModelVolume(this, other, std::move(mesh));
    this->volumes.push_back(v);
    v->center_geometry_after_creation();
    this->invalidate_bounding_box();
    return v;
}

void ModelObject::delete_volume(size_t idx)
{
    ModelVolumePtrs::iterator i = this->volumes.begin() + idx;
    delete *i;
    this->volumes.erase(i);

    if (this->volumes.size() == 1)
    {
        // only one volume left
        // we need to collapse the volume transform into the instances transforms because now when selecting this volume
        // it will be seen as a single full instance ans so its volume transform may be ignored
        ModelVolume* v = this->volumes.front();
        Transform3d v_t = v->get_transformation().get_matrix();
        for (ModelInstance* inst : this->instances)
        {
            inst->set_transformation(Geometry::Transformation(inst->get_transformation().get_matrix() * v_t));
        }
        Geometry::Transformation t;
        v->set_transformation(t);
        v->set_new_unique_id();
    }

    this->invalidate_bounding_box();
}

void ModelObject::clear_volumes()
{
    for (ModelVolume *v : this->volumes)
        delete v;
    this->volumes.clear();
    this->invalidate_bounding_box();
}

void ModelObject::sort_volumes(bool full_sort)
{
    // sort volumes inside the object to order "Model Part, Negative Volume, Modifier, Support Blocker and Support Enforcer. "
    if (full_sort)
        std::stable_sort(volumes.begin(), volumes.end(), [](ModelVolume* vl, ModelVolume* vr) {
            return vl->type() < vr->type();
        });
    // sort have to controll "place" of the support blockers/enforcers. But one of the model parts have to be on the first place.
    else
        std::stable_sort(volumes.begin(), volumes.end(), [](ModelVolume* vl, ModelVolume* vr) {
            ModelVolumeType vl_type = vl->type() > ModelVolumeType::PARAMETER_MODIFIER ? vl->type() : ModelVolumeType::PARAMETER_MODIFIER;
            ModelVolumeType vr_type = vr->type() > ModelVolumeType::PARAMETER_MODIFIER ? vr->type() : ModelVolumeType::PARAMETER_MODIFIER;
            return vl_type < vr_type;
        });
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

ModelInstance* ModelObject::add_instance(const Vec3d &offset, const Vec3d &scaling_factor, const Vec3d &rotation, const Vec3d &mirror)
{
    auto *instance = add_instance();
    instance->set_offset(offset);
    instance->set_scaling_factor(scaling_factor);
    instance->set_rotation(rotation);
    instance->set_mirror(mirror);
    return instance;
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
        m_bounding_box_valid = true;
        BoundingBoxf3 raw_bbox = this->raw_mesh_bounding_box();
        m_bounding_box.reset();
        for (const ModelInstance *i : this->instances)
            m_bounding_box.merge(i->transform_bounding_box(raw_bbox));
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
// Currently used by ModelObject::mesh(), to calculate the 2D envelope for 2D plater
// and to display the object statistics at ModelObject::print_info().
TriangleMesh ModelObject::raw_mesh() const
{
    TriangleMesh mesh;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part())
        {
            TriangleMesh vol_mesh(v->mesh());
            vol_mesh.transform(v->get_matrix());
            mesh.merge(vol_mesh);
        }
    return mesh;
}

// Non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
// Currently used by ModelObject::mesh(), to calculate the 2D envelope for 2D plater
// and to display the object statistics at ModelObject::print_info().
indexed_triangle_set ModelObject::raw_indexed_triangle_set() const
{
    size_t num_vertices = 0;
    size_t num_faces    = 0;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            num_vertices += v->mesh().its.vertices.size();
            num_faces    += v->mesh().its.indices.size();
        }
    indexed_triangle_set out;
    out.vertices.reserve(num_vertices);
    out.indices.reserve(num_faces);
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            size_t i = out.vertices.size();
            size_t j = out.indices.size();
            append(out.vertices, v->mesh().its.vertices);
            append(out.indices,  v->mesh().its.indices);
            auto m = v->get_matrix();
            for (; i < out.vertices.size(); ++ i)
                out.vertices[i] = (m * out.vertices[i].cast<double>()).cast<float>().eval();
            if (v->is_left_handed()) {
                for (; j < out.indices.size(); ++ j)
                    std::swap(out.indices[j][0], out.indices[j][1]);
            }
        }
    return out;
}


const BoundingBoxf3& ModelObject::raw_mesh_bounding_box() const
{
    if (! m_raw_mesh_bounding_box_valid) {
        m_raw_mesh_bounding_box_valid = true;
        m_raw_mesh_bounding_box.reset();
        for (const ModelVolume *v : this->volumes)
            if (v->is_model_part())
                m_raw_mesh_bounding_box.merge(v->mesh().transformed_bounding_box(v->get_matrix()));
    }
    return m_raw_mesh_bounding_box;
}

BoundingBoxf3 ModelObject::full_raw_mesh_bounding_box() const
{
	BoundingBoxf3 bb;
	for (const ModelVolume *v : this->volumes)
		bb.merge(v->mesh().transformed_bounding_box(v->get_matrix()));
	return bb;
}

// A transformed snug bounding box around the non-modifier object volumes, without the translation applied.
// This bounding box is only used for the actual slicing and for layer editing UI to calculate the layers.
const BoundingBoxf3& ModelObject::raw_bounding_box() const
{
    if (! m_raw_bounding_box_valid) {
        m_raw_bounding_box_valid = true;
        m_raw_bounding_box.reset();
        if (this->instances.empty())
            throw Slic3r::InvalidArgument("Can't call raw_bounding_box() with no instances");

        const Transform3d& inst_matrix = this->instances.front()->get_transformation().get_matrix(true);
        for (const ModelVolume *v : this->volumes)
            if (v->is_model_part())
                m_raw_bounding_box.merge(v->mesh().transformed_bounding_box(inst_matrix * v->get_matrix()));
    }
	return m_raw_bounding_box;
}

// This returns an accurate snug bounding box of the transformed object instance, without the translation applied.
BoundingBoxf3 ModelObject::instance_bounding_box(size_t instance_idx, bool dont_translate) const
{
    BoundingBoxf3 bb;
    const Transform3d& inst_matrix = this->instances[instance_idx]->get_transformation().get_matrix(dont_translate);
    for (ModelVolume *v : this->volumes)
    {
        if (v->is_model_part())
            bb.merge(v->mesh().transformed_bounding_box(inst_matrix * v->get_matrix()));
    }
    return bb;
}

// Calculate 2D convex hull of of a projection of the transformed printable volumes into the XY plane.
// This method is cheap in that it does not make any unnecessary copy of the volume meshes.
// This method is used by the auto arrange function.
#if ENABLE_ALLOW_NEGATIVE_Z
Polygon ModelObject::convex_hull_2d(const Transform3d& trafo_instance) const
{
    Points pts;
    for (const ModelVolume* v : volumes) {
        if (v->is_model_part())
            append(pts, its_convex_hull_2d_above(v->mesh().its, (trafo_instance * v->get_matrix()).cast<float>(), 0.0f).points);
    }
    return Geometry::convex_hull(std::move(pts));
}
#else
Polygon ModelObject::convex_hull_2d(const Transform3d &trafo_instance) const
{
    Points pts;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            Transform3d trafo = trafo_instance * v->get_matrix();
			const indexed_triangle_set &its = v->mesh().its;
			if (its.vertices.empty()) {
                // Using the STL faces.
				const stl_file& stl = v->mesh().stl;
				for (const stl_facet &facet : stl.facet_start)
                    for (size_t j = 0; j < 3; ++ j) {
                        Vec3d p = trafo * facet.vertex[j].cast<double>();
                            pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())));
                    }
            } else {
                // Using the shared vertices should be a bit quicker than using the STL faces.
                for (size_t i = 0; i < its.vertices.size(); ++ i) {
                    Vec3d p = trafo * its.vertices[i].cast<double>();
                        pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())));
                }
            }
        }
    return Geometry::convex_hull(std::move(pts));
}
#endif // ENABLE_ALLOW_NEGATIVE_Z

void ModelObject::center_around_origin(bool include_modifiers)
{
    // calculate the displacements needed to 
    // center this object around the origin
    const BoundingBoxf3 bb = include_modifiers ? full_raw_mesh_bounding_box() : raw_mesh_bounding_box();

    // Shift is the vector from the center of the bounding box to the origin
    const Vec3d shift = -bb.center();

    this->translate(shift);
    this->origin_translation += shift;
}

#if ENABLE_ALLOW_NEGATIVE_Z
void ModelObject::ensure_on_bed(bool allow_negative_z)
{
    const double min_z = get_min_z();
    if (!allow_negative_z || min_z > SINKING_Z_THRESHOLD)
        translate_instances({ 0.0, 0.0, -min_z });
}
#else
void ModelObject::ensure_on_bed()
{
    translate_instances({ 0.0, 0.0, -get_min_z() });
}
#endif // ENABLE_ALLOW_NEGATIVE_Z

void ModelObject::translate_instances(const Vec3d& vector)
{
    for (size_t i = 0; i < instances.size(); ++i) {
        translate_instance(i, vector);
    }
}

void ModelObject::translate_instance(size_t instance_idx, const Vec3d& vector)
{
    assert(instance_idx < instances.size());
    ModelInstance* i = instances[instance_idx];
    i->set_offset(i->get_offset() + vector);
    invalidate_bounding_box();
}

void ModelObject::translate(double x, double y, double z)
{
    for (ModelVolume *v : this->volumes) {
        v->translate(x, y, z);
    }

    if (m_bounding_box_valid)
        m_bounding_box.translate(x, y, z);
}

void ModelObject::scale(const Vec3d &versor)
{
    for (ModelVolume *v : this->volumes) {
        v->scale(versor);
    }
    this->invalidate_bounding_box();
}

void ModelObject::rotate(double angle, Axis axis)
{
    for (ModelVolume *v : this->volumes) {
        v->rotate(angle, axis);
    }
    center_around_origin();
    this->invalidate_bounding_box();
}

void ModelObject::rotate(double angle, const Vec3d& axis)
{
    for (ModelVolume *v : this->volumes) {
        v->rotate(angle, axis);
    }
    center_around_origin();
    this->invalidate_bounding_box();
}

void ModelObject::mirror(Axis axis)
{
    for (ModelVolume *v : this->volumes) {
        v->mirror(axis);
    }
    this->invalidate_bounding_box();
}

// This method could only be called before the meshes of this ModelVolumes are not shared!
void ModelObject::scale_mesh_after_creation(const Vec3d &versor)
{
    for (ModelVolume *v : this->volumes) {
        v->scale_geometry_after_creation(versor);
        v->set_offset(versor.cwiseProduct(v->get_offset()));
    }
    this->invalidate_bounding_box();
}

void ModelObject::convert_units(ModelObjectPtrs& new_objects, ConversionType conv_type, std::vector<int> volume_idxs)
{
    BOOST_LOG_TRIVIAL(trace) << "ModelObject::convert_units - start";

    ModelObject* new_object = new_clone(*this);

    double koef = conv_type == ConversionType::CONV_FROM_INCH   ? 25.4 : conv_type == ConversionType::CONV_TO_INCH  ? 0.0393700787  :
                  conv_type == ConversionType::CONV_FROM_METER  ? 1000 : conv_type == ConversionType::CONV_TO_METER ? 0.001         : 1;
    const Vec3d versor = Vec3d(koef, koef, koef);

    new_object->set_model(nullptr);
    new_object->sla_support_points.clear();
    new_object->sla_drain_holes.clear();
    new_object->sla_points_status = sla::PointsStatus::NoPoints;
    new_object->clear_volumes();
    new_object->input_file.clear();

    int vol_idx = 0;
    for (ModelVolume* volume : volumes)
    {
        if (!volume->mesh().empty()) {
            TriangleMesh mesh(volume->mesh());
            mesh.require_shared_vertices();

            ModelVolume* vol = new_object->add_volume(mesh);
            vol->name = volume->name;
            vol->set_type(volume->type());
            // Don't copy the config's ID.
            vol->config.assign_config(volume->config);
            assert(vol->config.id().valid());
            assert(vol->config.id() != volume->config.id());
            vol->set_material(volume->material_id(), *volume->material());
            vol->source.input_file = volume->source.input_file;
            vol->source.object_idx = (int)new_objects.size();
            vol->source.volume_idx = vol_idx;
            vol->source.is_converted_from_inches = volume->source.is_converted_from_inches;
            vol->source.is_converted_from_meters = volume->source.is_converted_from_meters;

            vol->supported_facets.assign(volume->supported_facets);
            vol->seam_facets.assign(volume->seam_facets);
            vol->mmu_segmentation_facets.assign(volume->mmu_segmentation_facets);

            // Perform conversion only if the target "imperial" state is different from the current one.
            // This check supports conversion of "mixed" set of volumes, each with different "imperial" state.
            if (//vol->source.is_converted_from_inches != from_imperial && 
                (volume_idxs.empty() || 
                 std::find(volume_idxs.begin(), volume_idxs.end(), vol_idx) != volume_idxs.end())) {
                vol->scale_geometry_after_creation(versor);
                vol->set_offset(versor.cwiseProduct(volume->get_offset()));
                if (conv_type == ConversionType::CONV_FROM_INCH || conv_type == ConversionType::CONV_TO_INCH)
                    vol->source.is_converted_from_inches = conv_type == ConversionType::CONV_FROM_INCH;
                if (conv_type == ConversionType::CONV_FROM_METER || conv_type == ConversionType::CONV_TO_METER)
                    vol->source.is_converted_from_meters = conv_type == ConversionType::CONV_FROM_METER;
            }
            else
                vol->set_offset(volume->get_offset());
        }
        vol_idx ++;
    }
    new_object->invalidate_bounding_box();

    new_objects.push_back(new_object);

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::convert_units - end";
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
            num += v->mesh().stl.stats.number_of_facets;
    return num;
}

bool ModelObject::needed_repair() const
{
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part() && v->mesh().needed_repair())
            return true;
    return false;
}

ModelObjectPtrs ModelObject::cut(size_t instance, coordf_t z, bool keep_upper, bool keep_lower, bool rotate_lower)
{
    if (!keep_upper && !keep_lower) { return {}; }

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::cut - start";

    // Clone the object to duplicate instances, materials etc.
    ModelObject* upper = keep_upper ? ModelObject::new_clone(*this) : nullptr;
    ModelObject* lower = keep_lower ? ModelObject::new_clone(*this) : nullptr;

    if (keep_upper) {
        upper->set_model(nullptr);
        upper->sla_support_points.clear();
        upper->sla_drain_holes.clear();
        upper->sla_points_status = sla::PointsStatus::NoPoints;
        upper->clear_volumes();
        upper->input_file.clear();
    }

    if (keep_lower) {
        lower->set_model(nullptr);
        lower->sla_support_points.clear();
        lower->sla_drain_holes.clear();
        lower->sla_points_status = sla::PointsStatus::NoPoints;
        lower->clear_volumes();
        lower->input_file.clear();
    }

    // Because transformations are going to be applied to meshes directly,
    // we reset transformation of all instances and volumes,
    // except for translation and Z-rotation on instances, which are preserved
    // in the transformation matrix and not applied to the mesh transform.

    // const auto instance_matrix = instances[instance]->get_matrix(true);
    const auto instance_matrix = Geometry::assemble_transform(
        Vec3d::Zero(),  // don't apply offset
        instances[instance]->get_rotation().cwiseProduct(Vec3d(1.0, 1.0, 0.0)),   // don't apply Z-rotation
        instances[instance]->get_scaling_factor(),
        instances[instance]->get_mirror()
    );

    z -= instances[instance]->get_offset()(2);

    // Lower part per-instance bounding boxes
    std::vector<BoundingBoxf3> lower_bboxes { instances.size() };

    for (ModelVolume *volume : volumes) {
        const auto volume_matrix = volume->get_matrix();

        volume->supported_facets.clear();
        volume->seam_facets.clear();
        volume->mmu_segmentation_facets.clear();

        if (! volume->is_model_part()) {
            // Modifiers are not cut, but we still need to add the instance transformation
            // to the modifier volume transformation to preserve their shape properly.

            volume->set_transformation(Geometry::Transformation(instance_matrix * volume_matrix));

            if (keep_upper) { upper->add_volume(*volume); }
            if (keep_lower) { lower->add_volume(*volume); }
        }
        else if (! volume->mesh().empty()) {
            
            // Transform the mesh by the combined transformation matrix.
            // Flip the triangles in case the composite transformation is left handed.
			TriangleMesh mesh(volume->mesh());
			mesh.transform(instance_matrix * volume_matrix, true);
			volume->reset_mesh();
            // Reset volume transformation except for offset
            const Vec3d offset = volume->get_offset();
            volume->set_transformation(Geometry::Transformation());
            volume->set_offset(offset);

            // Perform cut
            TriangleMesh upper_mesh, lower_mesh;
            {
                indexed_triangle_set upper_its, lower_its;
                mesh.require_shared_vertices();
                cut_mesh(mesh.its, float(z), &upper_its, &lower_its);
                if (keep_upper) {
                    upper_mesh = TriangleMesh(upper_its);
                    upper_mesh.repair();
                    upper_mesh.reset_repair_stats();
                }
                if (keep_lower) {
                    lower_mesh = TriangleMesh(lower_its);
                    lower_mesh.repair();
                    lower_mesh.reset_repair_stats();
                }
            }

            if (keep_upper && upper_mesh.facets_count() > 0) {
                ModelVolume* vol = upper->add_volume(upper_mesh);
                vol->name	= volume->name;
                // Don't copy the config's ID.
                vol->config.assign_config(volume->config);
    			assert(vol->config.id().valid());
	    		assert(vol->config.id() != volume->config.id());
                vol->set_material(volume->material_id(), *volume->material());
            }
            if (keep_lower && lower_mesh.facets_count() > 0) {
                ModelVolume* vol = lower->add_volume(lower_mesh);
                vol->name	= volume->name;
                // Don't copy the config's ID.
                vol->config.assign_config(volume->config);
                assert(vol->config.id().valid());
	    		assert(vol->config.id() != volume->config.id());
                vol->set_material(volume->material_id(), *volume->material());

                // Compute the lower part instances' bounding boxes to figure out where to place
                // the upper part
                if (keep_upper) {
                    for (size_t i = 0; i < instances.size(); i++) {
                        lower_bboxes[i].merge(instances[i]->transform_mesh_bounding_box(lower_mesh, true));
                    }
                }
            }
        }
    }

    ModelObjectPtrs res;

    if (keep_upper && upper->volumes.size() > 0) {
        upper->invalidate_bounding_box();
        upper->center_around_origin();

        // Reset instance transformation except offset and Z-rotation
        for (size_t i = 0; i < instances.size(); i++) {
            auto &instance = upper->instances[i];
            const Vec3d offset = instance->get_offset();
            const double rot_z = instance->get_rotation()(2);
            // The upper part displacement is set to half of the lower part bounding box
            // this is done in hope at least a part of the upper part will always be visible and draggable
            const Vec3d displace = lower_bboxes[i].size().cwiseProduct(Vec3d(-0.5, -0.5, 0.0));

            instance->set_transformation(Geometry::Transformation());
            instance->set_offset(offset + displace);
            instance->set_rotation(Vec3d(0.0, 0.0, rot_z));
        }

        res.push_back(upper);
    }
    if (keep_lower && lower->volumes.size() > 0) {
        lower->invalidate_bounding_box();
        lower->center_around_origin();

        // Reset instance transformation except offset and Z-rotation
        for (auto *instance : lower->instances) {
            const Vec3d offset = instance->get_offset();
            const double rot_z = instance->get_rotation()(2);

            instance->set_transformation(Geometry::Transformation());
            instance->set_offset(offset);
            instance->set_rotation(Vec3d(rotate_lower ? Geometry::deg2rad(180.0) : 0.0, 0.0, rot_z));
        }

        res.push_back(lower);
    }

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::cut - end";

    return res;
}

void ModelObject::split(ModelObjectPtrs* new_objects)
{
    for (ModelVolume* volume : this->volumes) {
        if (volume->type() != ModelVolumeType::MODEL_PART)
            continue;

        TriangleMeshPtrs meshptrs = volume->mesh().split();
        size_t counter = 1;
        for (TriangleMesh* mesh : meshptrs) {

            // FIXME: crashes if not satisfied
            if (mesh->facets_count() < 3) continue;

            mesh->repair();

            // XXX: this seems to be the only real usage of m_model, maybe refactor this so that it's not needed?
            ModelObject* new_object = m_model->add_object();
            if (meshptrs.size() == 1) {
                new_object->name = volume->name;
                // Don't copy the config's ID.
                new_object->config.assign_config(this->config.size() > 0 ? this->config : volume->config);
            }
            else {
                new_object->name = this->name + (meshptrs.size() > 1 ? "_" + std::to_string(counter++) : "");
                // Don't copy the config's ID.
                new_object->config.assign_config(this->config);
            }
            assert(new_object->config.id().valid());
            assert(new_object->config.id() != this->config.id());
            new_object->instances.reserve(this->instances.size());
            for (const ModelInstance* model_instance : this->instances)
                new_object->add_instance(*model_instance);
            ModelVolume* new_vol = new_object->add_volume(*volume, std::move(*mesh));

            for (ModelInstance* model_instance : new_object->instances)
            {
                Vec3d shift = model_instance->get_transformation().get_matrix(true) * new_vol->get_offset();
                model_instance->set_offset(model_instance->get_offset() + shift);
            }

            new_vol->set_offset(Vec3d::Zero());
            // reset the source to disable reload from disk
            new_vol->source = ModelVolume::Source();
            new_objects->emplace_back(new_object);
            delete mesh;
        }
    }
}


void ModelObject::merge()
{
    if (this->volumes.size() == 1) {
        // We can't merge meshes if there's just one volume
        return;
    }

    TriangleMesh mesh;

    for (ModelVolume* volume : volumes)
        if (!volume->mesh().empty())
            mesh.merge(volume->mesh());
    mesh.repair();

    this->clear_volumes();
    ModelVolume* vol = this->add_volume(mesh);

    if (!vol)
        return;
}

// Support for non-uniform scaling of instances. If an instance is rotated by angles, which are not multiples of ninety degrees,
// then the scaling in world coordinate system is not representable by the Geometry::Transformation structure.
// This situation is solved by baking in the instance transformation into the mesh vertices.
// Rotation and mirroring is being baked in. In case the instance scaling was non-uniform, it is baked in as well.
void ModelObject::bake_xy_rotation_into_meshes(size_t instance_idx)
{
    assert(instance_idx < this->instances.size());

	const Geometry::Transformation reference_trafo = this->instances[instance_idx]->get_transformation();
    if (Geometry::is_rotation_ninety_degrees(reference_trafo.get_rotation()))
        // nothing to do, scaling in the world coordinate space is possible in the representation of Geometry::Transformation.
        return;

    bool   left_handed        = reference_trafo.is_left_handed();
    bool   has_mirrorring     = ! reference_trafo.get_mirror().isApprox(Vec3d(1., 1., 1.));
    bool   uniform_scaling    = std::abs(reference_trafo.get_scaling_factor().x() - reference_trafo.get_scaling_factor().y()) < EPSILON &&
                                std::abs(reference_trafo.get_scaling_factor().x() - reference_trafo.get_scaling_factor().z()) < EPSILON;
    double new_scaling_factor = uniform_scaling ? reference_trafo.get_scaling_factor().x() : 1.;

    // Adjust the instances.
    for (size_t i = 0; i < this->instances.size(); ++ i) {
        ModelInstance &model_instance = *this->instances[i];
        model_instance.set_rotation(Vec3d(0., 0., Geometry::rotation_diff_z(reference_trafo.get_rotation(), model_instance.get_rotation())));
        model_instance.set_scaling_factor(Vec3d(new_scaling_factor, new_scaling_factor, new_scaling_factor));
        model_instance.set_mirror(Vec3d(1., 1., 1.));
    }

    // Adjust the meshes.
    // Transformation to be applied to the meshes.
    Eigen::Matrix3d mesh_trafo_3x3           = reference_trafo.get_matrix(true, false, uniform_scaling, ! has_mirrorring).matrix().block<3, 3>(0, 0);
	Transform3d     volume_offset_correction = this->instances[instance_idx]->get_transformation().get_matrix().inverse() * reference_trafo.get_matrix();
    for (ModelVolume *model_volume : this->volumes) {
        const Geometry::Transformation volume_trafo = model_volume->get_transformation();
        bool   volume_left_handed        = volume_trafo.is_left_handed();
        bool   volume_has_mirrorring     = ! volume_trafo.get_mirror().isApprox(Vec3d(1., 1., 1.));
        bool   volume_uniform_scaling    = std::abs(volume_trafo.get_scaling_factor().x() - volume_trafo.get_scaling_factor().y()) < EPSILON &&
                                           std::abs(volume_trafo.get_scaling_factor().x() - volume_trafo.get_scaling_factor().z()) < EPSILON;
        double volume_new_scaling_factor = volume_uniform_scaling ? volume_trafo.get_scaling_factor().x() : 1.;
        // Transform the mesh.
		Matrix3d volume_trafo_3x3 = volume_trafo.get_matrix(true, false, volume_uniform_scaling, !volume_has_mirrorring).matrix().block<3, 3>(0, 0);
        // Following method creates a new shared_ptr<TriangleMesh>
		model_volume->transform_this_mesh(mesh_trafo_3x3 * volume_trafo_3x3, left_handed != volume_left_handed);
        // Reset the rotation, scaling and mirroring.
        model_volume->set_rotation(Vec3d(0., 0., 0.));
        model_volume->set_scaling_factor(Vec3d(volume_new_scaling_factor, volume_new_scaling_factor, volume_new_scaling_factor));
        model_volume->set_mirror(Vec3d(1., 1., 1.));
        // Move the reference point of the volume to compensate for the change of the instance trafo.
        model_volume->set_offset(volume_offset_correction * volume_trafo.get_offset());
        // reset the source to disable reload from disk
        model_volume->source = ModelVolume::Source();
    }

    this->invalidate_bounding_box();
}

double ModelObject::get_min_z() const
{
    if (instances.empty())
        return 0.0;
    else {
        double min_z = DBL_MAX;
        for (size_t i = 0; i < instances.size(); ++i) {
            min_z = std::min(min_z, get_instance_min_z(i));
        }
        return min_z;
    }
}

double ModelObject::get_instance_min_z(size_t instance_idx) const
{
    double min_z = DBL_MAX;

    const ModelInstance* inst = instances[instance_idx];
    const Transform3d& mi = inst->get_matrix(true);

    for (const ModelVolume* v : volumes) {
        if (!v->is_model_part())
            continue;

        const Transform3d mv = mi * v->get_matrix();
        const TriangleMesh& hull = v->get_convex_hull();
		for (const stl_facet &facet : hull.stl.facet_start)
			for (int i = 0; i < 3; ++ i)
				min_z = std::min(min_z, (mv * facet.vertex[i].cast<double>()).z());
    }

    return min_z + inst->get_offset(Z);
}

unsigned int ModelObject::check_instances_print_volume_state(const BoundingBoxf3& print_volume)
{
    unsigned int num_printable = 0;
    enum {
        INSIDE  = 1,
        OUTSIDE = 2
    };
    for (ModelInstance *model_instance : this->instances) {
        unsigned int inside_outside = 0;
        for (const ModelVolume *vol : this->volumes)
            if (vol->is_model_part()) {
                BoundingBoxf3 bb = vol->get_convex_hull().transformed_bounding_box(model_instance->get_matrix() * vol->get_matrix());
                if (print_volume.contains(bb))
                    inside_outside |= INSIDE;
                else if (print_volume.intersects(bb))
                    inside_outside |= INSIDE | OUTSIDE;
                else
                    inside_outside |= OUTSIDE;
            }
        model_instance->print_volume_state = 
            (inside_outside == (INSIDE | OUTSIDE)) ? ModelInstancePVS_Partly_Outside :
            (inside_outside == INSIDE) ? ModelInstancePVS_Inside : ModelInstancePVS_Fully_Outside;
        if (inside_outside == INSIDE)
            ++ num_printable;
    }
    return num_printable;
}

void ModelObject::print_info() const
{
    using namespace std;
    cout << fixed;
    boost::nowide::cout << "[" << boost::filesystem::path(this->input_file).filename().string() << "]" << endl;
    
    TriangleMesh mesh = this->raw_mesh();
    mesh.check_topology();
    BoundingBoxf3 bb = mesh.bounding_box();
    Vec3d size = bb.size();
    cout << "size_x = " << size(0) << endl;
    cout << "size_y = " << size(1) << endl;
    cout << "size_z = " << size(2) << endl;
    cout << "min_x = " << bb.min(0) << endl;
    cout << "min_y = " << bb.min(1) << endl;
    cout << "min_z = " << bb.min(2) << endl;
    cout << "max_x = " << bb.max(0) << endl;
    cout << "max_y = " << bb.max(1) << endl;
    cout << "max_z = " << bb.max(2) << endl;
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

std::string ModelObject::get_export_filename() const
{
    std::string ret = input_file;

    if (!name.empty())
    {
        if (ret.empty())
            // input_file was empty, just use name
            ret = name;
        else
        {
            // Replace file name in input_file with name, but keep the path and file extension.
            ret = (boost::filesystem::path(name).parent_path().empty()) ?
                (boost::filesystem::path(ret).parent_path() / name).make_preferred().string() : name;
        }
    }

    return ret;
}

stl_stats ModelObject::get_object_stl_stats() const
{
    if (this->volumes.size() == 1)
        return this->volumes[0]->mesh().stl.stats;

    stl_stats full_stats;
    full_stats.volume = 0.f;

    // fill full_stats from all objet's meshes
    for (ModelVolume* volume : this->volumes)
    {
        const stl_stats& stats = volume->mesh().stl.stats;

        // initialize full_stats (for repaired errors)
        full_stats.degenerate_facets    += stats.degenerate_facets;
        full_stats.edges_fixed          += stats.edges_fixed;
        full_stats.facets_removed       += stats.facets_removed;
        full_stats.facets_added         += stats.facets_added;
        full_stats.facets_reversed      += stats.facets_reversed;
        full_stats.backwards_edges      += stats.backwards_edges;

        // another used satistics value
        if (volume->is_model_part()) {
            full_stats.volume           += stats.volume;
            full_stats.number_of_parts  += stats.number_of_parts;
        }
    }

    return full_stats;
}

int ModelObject::get_mesh_errors_count(const int vol_idx /*= -1*/) const
{
    if (vol_idx >= 0)
        return this->volumes[vol_idx]->get_mesh_errors_count();

    const stl_stats& stats = get_object_stl_stats();

    return  stats.degenerate_facets + stats.edges_fixed     + stats.facets_removed +
            stats.facets_added      + stats.facets_reversed + stats.backwards_edges;
}

void ModelVolume::set_material_id(t_model_material_id material_id)
{
    m_material_id = material_id;
    // ensure m_material_id references an existing material
    if (! material_id.empty())
        this->object->get_model()->add_material(material_id);
}

ModelMaterial* ModelVolume::material() const
{ 
    return this->object->get_model()->get_material(m_material_id);
}

void ModelVolume::set_material(t_model_material_id material_id, const ModelMaterial &material)
{
    m_material_id = material_id;
    if (! material_id.empty())
        this->object->get_model()->add_material(material_id, material);
}

// Extract the current extruder ID based on this ModelVolume's config and the parent ModelObject's config.
int ModelVolume::extruder_id() const
{
    int extruder_id = -1;
    if (this->is_model_part()) {
        const ConfigOption *opt = this->config.option("extruder");
        if ((opt == nullptr) || (opt->getInt() == 0))
            opt = this->object->config.option("extruder");
        extruder_id = (opt == nullptr) ? 0 : opt->getInt();
    }
    return extruder_id;
}

bool ModelVolume::is_splittable() const
{
    // the call mesh.is_splittable() is expensive, so cache the value to calculate it only once
    if (m_is_splittable == -1)
        m_is_splittable = (int)this->mesh().is_splittable();

    return m_is_splittable == 1;
}

void ModelVolume::center_geometry_after_creation(bool update_source_offset)
{
    Vec3d shift = this->mesh().bounding_box().center();
    if (!shift.isApprox(Vec3d::Zero()))
    {
    	if (m_mesh)
        	const_cast<TriangleMesh*>(m_mesh.get())->translate(-(float)shift(0), -(float)shift(1), -(float)shift(2));
        if (m_convex_hull)
			const_cast<TriangleMesh*>(m_convex_hull.get())->translate(-(float)shift(0), -(float)shift(1), -(float)shift(2));
        translate(shift);
    }

    if (update_source_offset)
        source.mesh_offset = shift;
}

void ModelVolume::calculate_convex_hull()
{
    m_convex_hull = std::make_shared<TriangleMesh>(this->mesh().convex_hull_3d());
}

int ModelVolume::get_mesh_errors_count() const
{
    const stl_stats& stats = this->mesh().stl.stats;

    return  stats.degenerate_facets + stats.edges_fixed     + stats.facets_removed +
            stats.facets_added      + stats.facets_reversed + stats.backwards_edges;
}

const TriangleMesh& ModelVolume::get_convex_hull() const
{
    return *m_convex_hull.get();
}

ModelVolumeType ModelVolume::type_from_string(const std::string &s)
{
    // Legacy support
    if (s == "1")
		return ModelVolumeType::PARAMETER_MODIFIER;
    // New type (supporting the support enforcers & blockers)
    if (s == "ModelPart")
		return ModelVolumeType::MODEL_PART;
    if (s == "NegativeVolume")
        return ModelVolumeType::NEGATIVE_VOLUME;
    if (s == "ParameterModifier")
		return ModelVolumeType::PARAMETER_MODIFIER;
    if (s == "SupportEnforcer")
		return ModelVolumeType::SUPPORT_ENFORCER;
    if (s == "SupportBlocker")
		return ModelVolumeType::SUPPORT_BLOCKER;
    assert(s == "0");
    // Default value if invalud type string received.
	return ModelVolumeType::MODEL_PART;
}

std::string ModelVolume::type_to_string(const ModelVolumeType t)
{
    switch (t) {
	case ModelVolumeType::MODEL_PART:         return "ModelPart";
    case ModelVolumeType::NEGATIVE_VOLUME:    return "NegativeVolume";
	case ModelVolumeType::PARAMETER_MODIFIER: return "ParameterModifier";
	case ModelVolumeType::SUPPORT_ENFORCER:   return "SupportEnforcer";
	case ModelVolumeType::SUPPORT_BLOCKER:    return "SupportBlocker";
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
    TriangleMeshPtrs meshptrs = this->mesh().split();
    if (meshptrs.size() <= 1) {
        delete meshptrs.front();
        return 1;
    }

    size_t idx = 0;
    size_t ivolume = std::find(this->object->volumes.begin(), this->object->volumes.end(), this) - this->object->volumes.begin();
    std::string name = this->name;

    unsigned int extruder_counter = 0;
    Vec3d offset = this->get_offset();

    for (TriangleMesh *mesh : meshptrs) {
        mesh->repair();
        if (idx == 0)
        {
            this->set_mesh(std::move(*mesh));
            this->calculate_convex_hull();
            // Assign a new unique ID, so that a new GLVolume will be generated.
            this->set_new_unique_id();
            // reset the source to disable reload from disk
            this->source = ModelVolume::Source();
        }
        else
            this->object->volumes.insert(this->object->volumes.begin() + (++ivolume), new ModelVolume(object, *this, std::move(*mesh)));

        this->object->volumes[ivolume]->set_offset(Vec3d::Zero());
        this->object->volumes[ivolume]->center_geometry_after_creation();
        this->object->volumes[ivolume]->translate(offset);
        this->object->volumes[ivolume]->name = name + "_" + std::to_string(idx + 1);
        this->object->volumes[ivolume]->config.set_deserialize("extruder", auto_extruder_id(max_extruders, extruder_counter));
        this->object->volumes[ivolume]->m_is_splittable = 0;
        delete mesh;
        ++ idx;
    }
    
    return idx;
}

void ModelVolume::translate(const Vec3d& displacement)
{
    set_offset(get_offset() + displacement);
}

void ModelVolume::scale(const Vec3d& scaling_factors)
{
    set_scaling_factor(get_scaling_factor().cwiseProduct(scaling_factors));
}

void ModelObject::scale_to_fit(const Vec3d &size)
{
/*
    BoundingBoxf3 instance_bounding_box(size_t instance_idx, bool dont_translate = false) const;
    Vec3d orig_size = this->bounding_box().size();
    float factor = fminf(
        size.x / orig_size.x,
        fminf(
            size.y / orig_size.y,
            size.z / orig_size.z
        )
    );
    this->scale(factor);
*/
}

void ModelVolume::assign_new_unique_ids_recursive()
{
    ObjectBase::set_new_unique_id();
    config.set_new_unique_id();
    supported_facets.set_new_unique_id();
    seam_facets.set_new_unique_id();
    mmu_segmentation_facets.set_new_unique_id();
}

void ModelVolume::rotate(double angle, Axis axis)
{
    switch (axis)
    {
    case X: { rotate(angle, Vec3d::UnitX()); break; }
    case Y: { rotate(angle, Vec3d::UnitY()); break; }
    case Z: { rotate(angle, Vec3d::UnitZ()); break; }
    default: break;
    }
}

void ModelVolume::rotate(double angle, const Vec3d& axis)
{
    set_rotation(get_rotation() + Geometry::extract_euler_angles(Eigen::Quaterniond(Eigen::AngleAxisd(angle, axis)).toRotationMatrix()));
}

void ModelVolume::mirror(Axis axis)
{
    Vec3d mirror = get_mirror();
    switch (axis)
    {
    case X: { mirror(0) *= -1.0; break; }
    case Y: { mirror(1) *= -1.0; break; }
    case Z: { mirror(2) *= -1.0; break; }
    default: break;
    }
    set_mirror(mirror);
}

// This method could only be called before the meshes of this ModelVolumes are not shared!
void ModelVolume::scale_geometry_after_creation(const Vec3d& versor)
{
	const_cast<TriangleMesh*>(m_mesh.get())->scale(versor);
	const_cast<TriangleMesh*>(m_convex_hull.get())->scale(versor);
}

void ModelVolume::transform_this_mesh(const Transform3d &mesh_trafo, bool fix_left_handed)
{
	TriangleMesh mesh = this->mesh();
	mesh.transform(mesh_trafo, fix_left_handed);
	this->set_mesh(std::move(mesh));
    TriangleMesh convex_hull = this->get_convex_hull();
    convex_hull.transform(mesh_trafo, fix_left_handed);
    m_convex_hull = std::make_shared<TriangleMesh>(std::move(convex_hull));
    // Let the rest of the application know that the geometry changed, so the meshes have to be reloaded.
    this->set_new_unique_id();
}

void ModelVolume::transform_this_mesh(const Matrix3d &matrix, bool fix_left_handed)
{
	TriangleMesh mesh = this->mesh();
	mesh.transform(matrix, fix_left_handed);
	this->set_mesh(std::move(mesh));
    TriangleMesh convex_hull = this->get_convex_hull();
    convex_hull.transform(matrix, fix_left_handed);
    m_convex_hull = std::make_shared<TriangleMesh>(std::move(convex_hull));
    // Let the rest of the application know that the geometry changed, so the meshes have to be reloaded.
    this->set_new_unique_id();
}

void ModelVolume::convert_from_imperial_units()
{
    double in_to_mm = 25.4;
    this->scale_geometry_after_creation(Vec3d(in_to_mm, in_to_mm, in_to_mm));
    this->set_offset(Vec3d(0, 0, 0));
    this->source.is_converted_from_inches = true;
}

void ModelVolume::convert_from_meters()
{
    double m_to_mm = 1000;
    this->scale_geometry_after_creation(Vec3d(m_to_mm, m_to_mm, m_to_mm));
    this->set_offset(Vec3d(0, 0, 0));
    this->source.is_converted_from_meters = true;
}

void ModelInstance::transform_mesh(TriangleMesh* mesh, bool dont_translate) const
{
    mesh->transform(get_matrix(dont_translate));
}

BoundingBoxf3 ModelInstance::transform_mesh_bounding_box(const TriangleMesh& mesh, bool dont_translate) const
{
    // Rotate around mesh origin.
    TriangleMesh copy(mesh);
    copy.transform(get_matrix(true, false, true, true));
    BoundingBoxf3 bbox = copy.bounding_box();

    if (!empty(bbox)) {
        // Scale the bounding box along the three axes.
        for (unsigned int i = 0; i < 3; ++i)
        {
            if (std::abs(get_scaling_factor((Axis)i)-1.0) > EPSILON)
            {
                bbox.min(i) *= get_scaling_factor((Axis)i);
                bbox.max(i) *= get_scaling_factor((Axis)i);
            }
        }

        // Translate the bounding box.
        if (! dont_translate) {
            bbox.min += get_offset();
            bbox.max += get_offset();
        }
    }
    return bbox;
}

BoundingBoxf3 ModelInstance::transform_bounding_box(const BoundingBoxf3 &bbox, bool dont_translate) const
{
    return bbox.transformed(get_matrix(dont_translate));
}

Vec3d ModelInstance::transform_vector(const Vec3d& v, bool dont_translate) const
{
    return get_matrix(dont_translate) * v;
}

void ModelInstance::transform_polygon(Polygon* polygon) const
{
    // CHECK_ME -> Is the following correct or it should take in account all three rotations ?
    polygon->rotate(get_rotation(Z)); // rotate around polygon origin
    // CHECK_ME -> Is the following correct ?
    polygon->scale(get_scaling_factor(X), get_scaling_factor(Y)); // scale around polygon origin
}

arrangement::ArrangePolygon ModelInstance::get_arrange_polygon() const
{
//    static const double SIMPLIFY_TOLERANCE_MM = 0.1;
    
    Vec3d rotation = get_rotation();
    rotation.z()   = 0.;
    Transform3d trafo_instance =
#if ENABLE_ALLOW_NEGATIVE_Z
        Geometry::assemble_transform(get_offset().z() * Vec3d::UnitZ(), rotation,
                                     get_scaling_factor(), get_mirror());
#else
        Geometry::assemble_transform(Vec3d::Zero(), rotation,
                                     get_scaling_factor(), get_mirror());
#endif // ENABLE_ALLOW_NEGATIVE_Z

    Polygon p = get_object()->convex_hull_2d(trafo_instance);

#if !ENABLE_ALLOW_NEGATIVE_Z
    assert(!p.points.empty());
#endif // !ENABLE_ALLOW_NEGATIVE_Z

//    if (!p.points.empty()) {
//        Polygons pp{p};
//        pp = p.simplify(scaled<double>(SIMPLIFY_TOLERANCE_MM));
//        if (!pp.empty()) p = pp.front();
//    }
   
    arrangement::ArrangePolygon ret;
    ret.poly.contour = std::move(p);
    ret.translation  = Vec2crd{scaled(get_offset(X)), scaled(get_offset(Y))};
    ret.rotation     = get_rotation(Z);

    return ret;
}

indexed_triangle_set FacetsAnnotation::get_facets(const ModelVolume& mv, EnforcerBlockerType type) const
{
    TriangleSelector selector(mv.mesh());
    selector.deserialize(m_data);
    indexed_triangle_set out = selector.get_facets(type);
    return out;
}

bool FacetsAnnotation::set(const TriangleSelector& selector)
{
    std::map<int, std::vector<bool>> sel_map = selector.serialize();
    if (sel_map != m_data) {
        m_data = sel_map;
        this->touch();
        return true;
    }
    return false;
}

void FacetsAnnotation::clear()
{
    m_data.clear();
    this->reset_timestamp();
}

// Following function takes data from a triangle and encodes it as string
// of hexadecimal numbers (one digit per triangle). Used for 3MF export,
// changing it may break backwards compatibility !!!!!
std::string FacetsAnnotation::get_triangle_as_string(int triangle_idx) const
{
    std::string out;

    auto triangle_it = m_data.find(triangle_idx);
    if (triangle_it != m_data.end()) {
        const std::vector<bool>& code = triangle_it->second;
        int offset = 0;
        while (offset < int(code.size())) {
            int next_code = 0;
            for (int i=3; i>=0; --i) {
                next_code = next_code << 1;
                next_code |= int(code[offset + i]);
            }
            offset += 4;

            assert(next_code >=0 && next_code <= 15);
            char digit = next_code < 10 ? next_code + '0' : (next_code-10)+'A';
            out.insert(out.begin(), digit);
        }
    }
    return out;
}

// Recover triangle splitting & state from string of hexadecimal values previously
// generated by get_triangle_as_string. Used to load from 3MF.
void FacetsAnnotation::set_triangle_from_string(int triangle_id, const std::string& str)
{
    assert(! str.empty());
    m_data[triangle_id] = std::vector<bool>(); // zero current state or create new
    std::vector<bool>& code = m_data[triangle_id];

    for (auto it = str.crbegin(); it != str.crend(); ++it) {
        const char ch = *it;
        int dec = 0;
        if (ch >= '0' && ch<='9')
            dec = int(ch - '0');
        else if (ch >='A' && ch <= 'F')
            dec = 10 + int(ch - 'A');
        else
            assert(false);

        // Convert to binary and append into code.
        for (int i=0; i<4; ++i) {
            code.insert(code.end(), bool(dec & (1 << i)));
        }
    }
}

// Test whether the two models contain the same number of ModelObjects with the same set of IDs
// ordered in the same order. In that case it is not necessary to kill the background processing.
bool model_object_list_equal(const Model &model_old, const Model &model_new)
{
    if (model_old.objects.size() != model_new.objects.size())
        return false;
    for (size_t i = 0; i < model_old.objects.size(); ++ i)
        if (model_old.objects[i]->id() != model_new.objects[i]->id())
            return false;
    return true;
}

// Test whether the new model is just an extension of the old model (new objects were added
// to the end of the original list. In that case it is not necessary to kill the background processing.
bool model_object_list_extended(const Model &model_old, const Model &model_new)
{
    if (model_old.objects.size() >= model_new.objects.size())
        return false;
    for (size_t i = 0; i < model_old.objects.size(); ++ i)
        if (model_old.objects[i]->id() != model_new.objects[i]->id())
            return false;
    return true;
}

template<typename TypeFilterFn>
bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, TypeFilterFn type_filter)
{
    size_t i_old, i_new;
    for (i_old = 0, i_new = 0; i_old < model_object_old.volumes.size() && i_new < model_object_new.volumes.size();) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (! type_filter(mv_old.type())) {
            ++ i_old;
            continue;
        }
        if (! type_filter(mv_new.type())) {
            ++ i_new;
            continue;
        }
        if (mv_old.type() != mv_new.type() || mv_old.id() != mv_new.id())
            return true;
        //FIXME test for the content of the mesh!
        if (! mv_old.get_matrix().isApprox(mv_new.get_matrix()))
            return true;
        ++ i_old;
        ++ i_new;
    }
    for (; i_old < model_object_old.volumes.size(); ++ i_old) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        if (type_filter(mv_old.type()))
            // ModelVolume was deleted.
            return true;
    }
    for (; i_new < model_object_new.volumes.size(); ++ i_new) {
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (type_filter(mv_new.type()))
            // ModelVolume was added.
            return true;
    }
    return false;
}

bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, const ModelVolumeType type)
{
    return model_volume_list_changed(model_object_old, model_object_new, [type](const ModelVolumeType t) { return t == type; });
}

bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, const std::initializer_list<ModelVolumeType> &types)
{
    return model_volume_list_changed(model_object_old, model_object_new, [&types](const ModelVolumeType t) {
        return std::find(types.begin(), types.end(), t) != types.end();
    });
}

template< typename TypeFilterFn, typename CompareFn>
bool model_property_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, TypeFilterFn type_filter, CompareFn compare)
{
    assert(! model_volume_list_changed(model_object_old, model_object_new, type_filter));
    size_t i_old, i_new;
    for (i_old = 0, i_new = 0; i_old < model_object_old.volumes.size() && i_new < model_object_new.volumes.size();) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (! type_filter(mv_old.type())) {
            ++ i_old;
            continue;
        }
        if (! type_filter(mv_new.type())) {
            ++ i_new;
            continue;
        }
        assert(mv_old.type() == mv_new.type() && mv_old.id() == mv_new.id());
        if (! compare(mv_old, mv_new))
            return true;
        ++ i_old;
        ++ i_new;
    }
    return false;
}

bool model_custom_supports_data_changed(const ModelObject& mo, const ModelObject& mo_new)
{
    return model_property_changed(mo, mo_new, 
        [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; }, 
        [](const ModelVolume &mv_old, const ModelVolume &mv_new){ return mv_old.supported_facets.timestamp_matches(mv_new.supported_facets); });
}

bool model_custom_seam_data_changed(const ModelObject& mo, const ModelObject& mo_new)
{
    return model_property_changed(mo, mo_new, 
        [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; }, 
        [](const ModelVolume &mv_old, const ModelVolume &mv_new){ return mv_old.seam_facets.timestamp_matches(mv_new.seam_facets); });
}

bool model_mmu_segmentation_data_changed(const ModelObject& mo, const ModelObject& mo_new)
{
    return model_property_changed(mo, mo_new, 
        [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; }, 
        [](const ModelVolume &mv_old, const ModelVolume &mv_new){ return mv_old.mmu_segmentation_facets.timestamp_matches(mv_new.mmu_segmentation_facets); });
}

bool model_has_multi_part_objects(const Model &model)
{
    for (const ModelObject *model_object : model.objects)
    	if (model_object->volumes.size() != 1 || ! model_object->volumes.front()->is_model_part())
    		return true;
    return false;
}

bool model_has_advanced_features(const Model &model)
{
	auto config_is_advanced = [](const ModelConfig &config) {
        return ! (config.empty() || (config.size() == 1 && config.cbegin()->first == "extruder"));
	};
    for (const ModelObject *model_object : model.objects) {
        // Is there more than one instance or advanced config data?
        if (model_object->instances.size() > 1 || config_is_advanced(model_object->config))
        	return true;
        // Is there any modifier or advanced config data?
        for (const ModelVolume* model_volume : model_object->volumes)
            if (! model_volume->is_model_part() || config_is_advanced(model_volume->config))
            	return true;
    }
    return false;
}

#ifndef NDEBUG
// Verify whether the IDs of Model / ModelObject / ModelVolume / ModelInstance / ModelMaterial are valid and unique.
void check_model_ids_validity(const Model &model)
{
    std::set<ObjectID> ids;
    auto check = [&ids](ObjectID id) { 
        assert(id.valid());
        assert(ids.find(id) == ids.end());
        ids.insert(id);
    };
    for (const ModelObject *model_object : model.objects) {
        check(model_object->id());
        check(model_object->config.id());
        for (const ModelVolume *model_volume : model_object->volumes) {
            check(model_volume->id());
	        check(model_volume->config.id());
        }
        for (const ModelInstance *model_instance : model_object->instances)
            check(model_instance->id());
    }
    for (const auto mm : model.materials) {
        check(mm.second->id());
        check(mm.second->config.id());
    }
}

void check_model_ids_equal(const Model &model1, const Model &model2)
{
    // Verify whether the IDs of model1 and model match.
    assert(model1.objects.size() == model2.objects.size());
    for (size_t idx_model = 0; idx_model < model2.objects.size(); ++ idx_model) {
        const ModelObject &model_object1 = *model1.objects[idx_model];
        const ModelObject &model_object2 = *  model2.objects[idx_model];
        assert(model_object1.id() == model_object2.id());
        assert(model_object1.config.id() == model_object2.config.id());
        assert(model_object1.volumes.size() == model_object2.volumes.size());
        assert(model_object1.instances.size() == model_object2.instances.size());
        for (size_t i = 0; i < model_object1.volumes.size(); ++ i) {
            assert(model_object1.volumes[i]->id() == model_object2.volumes[i]->id());
        	assert(model_object1.volumes[i]->config.id() == model_object2.volumes[i]->config.id());
        }
        for (size_t i = 0; i < model_object1.instances.size(); ++ i)
            assert(model_object1.instances[i]->id() == model_object2.instances[i]->id());
    }
    assert(model1.materials.size() == model2.materials.size());
    {
        auto it1 = model1.materials.begin();
        auto it2 = model2.materials.begin();
        for (; it1 != model1.materials.end(); ++ it1, ++ it2) {
            assert(it1->first == it2->first); // compare keys
            assert(it1->second->id() == it2->second->id());
        	assert(it1->second->config.id() == it2->second->config.id());
        }
    }
}

#endif /* NDEBUG */

}

#if 0
CEREAL_REGISTER_TYPE(Slic3r::ModelObject)
CEREAL_REGISTER_TYPE(Slic3r::ModelVolume)
CEREAL_REGISTER_TYPE(Slic3r::ModelInstance)
CEREAL_REGISTER_TYPE(Slic3r::Model)

CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ObjectBase, Slic3r::ModelObject)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ObjectBase, Slic3r::ModelVolume)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ObjectBase, Slic3r::ModelInstance)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ObjectBase, Slic3r::Model)
#endif
