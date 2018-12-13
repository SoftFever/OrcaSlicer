#include "SLAPrint.hpp"
#include "SLA/SLASupportTree.hpp"
#include "SLA/SLABasePool.hpp"
#include "MTUtils.hpp"

#include <unordered_set>
#include <numeric>

#include <tbb/parallel_for.h>
#include <boost/log/trivial.hpp>

//#include <tbb/spin_mutex.h>//#include "tbb/mutex.h"

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

using SlicedModel = SlicedSupports;
using SupportTreePtr = std::unique_ptr<sla::SLASupportTree>;

class SLAPrintObject::SupportData {
public:
    sla::EigenMesh3D emesh;              // index-triangle representation
    sla::PointSet    support_points;     // all the support points (manual/auto)
    SupportTreePtr   support_tree_ptr;   // the supports
    SlicedSupports   support_slices;     // sliced supports
    std::vector<LevelID>    level_ids;
};

namespace {

// should add up to 100 (%)
const std::array<unsigned, slaposCount>     OBJ_STEP_LEVELS =
{
    10,     // slaposObjectSlice,
    10,     // slaposSupportIslands,
    20,     // slaposSupportPoints,
    25,     // slaposSupportTree,
    25,     // slaposBasePool,
    5,      // slaposSliceSupports,
    5       // slaposIndexSlices
};

const std::array<std::string, slaposCount> OBJ_STEP_LABELS =
{
    L("Slicing model"),                 // slaposObjectSlice,
    L("Generating islands"),            // slaposSupportIslands,
    L("Scanning model structure"),      // slaposSupportPoints,
    L("Generating support tree"),       // slaposSupportTree,
    L("Generating base pool"),          // slaposBasePool,
    L("Slicing supports"),              // slaposSliceSupports,
    L("Slicing supports")               // slaposIndexSlices,
};

// Should also add up to 100 (%)
const std::array<unsigned, slapsCount> PRINT_STEP_LEVELS =
{
    80,     // slapsRasterize
    20,     // slapsValidate
};

const std::array<std::string, slapsCount> PRINT_STEP_LABELS =
{
    L("Rasterizing layers"),         // slapsRasterize
    L("Validating"),                 // slapsValidate
};

}

void SLAPrint::clear()
{
    tbb::mutex::scoped_lock lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
    for (SLAPrintObject *object : m_objects)
        delete object;
    m_objects.clear();
    m_model.clear_objects();
}

// Transformation without rotation around Z and without a shift by X and Y.
static Transform3d sla_trafo(const ModelObject &model_object)
{
    ModelInstance &model_instance = *model_object.instances.front();
    Vec3d          offset         = model_instance.get_offset();
    Vec3d          rotation       = model_instance.get_rotation();
    offset(0) = 0.;
    offset(1) = 0.;
    rotation(2) = 0.;
    return Geometry::assemble_transform(offset, rotation, model_instance.get_scaling_factor(), model_instance.get_mirror());
}

// List of instances, where the ModelInstance transformation is a composite of sla_trafo and the transformation defined by SLAPrintObject::Instance.
static std::vector<SLAPrintObject::Instance> sla_instances(const ModelObject &model_object)
{
    std::vector<SLAPrintObject::Instance> instances;
    for (ModelInstance *model_instance : model_object.instances)
        if (model_instance->is_printable()) {
            instances.emplace_back(SLAPrintObject::Instance(
                model_instance->id(),
                Point::new_scale(model_instance->get_offset(X), model_instance->get_offset(Y)),
                float(model_instance->get_rotation(Z))));
        }
    return instances;
}

SLAPrint::ApplyStatus SLAPrint::apply(const Model &model, const DynamicPrintConfig &config_in)
{
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    // Make a copy of the config, normalize it.
    DynamicPrintConfig config(config_in);
    config.normalize();
    // Collect changes to print config.
    t_config_option_keys print_diff    = m_print_config.diff(config);    
    t_config_option_keys printer_diff  = m_printer_config.diff(config);
    t_config_option_keys material_diff = m_material_config.diff(config);
    t_config_option_keys object_diff   = m_default_object_config.diff(config);
    t_config_option_keys placeholder_parser_diff = this->placeholder_parser().config_diff(config);

    // Do not use the ApplyStatus as we will use the max function when updating apply_status.
    unsigned int apply_status = APPLY_STATUS_UNCHANGED;
    auto update_apply_status = [&apply_status](bool invalidated)
        { apply_status = std::max<unsigned int>(apply_status, invalidated ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED); };
    if (! (print_diff.empty() && printer_diff.empty() && material_diff.empty() && object_diff.empty()))
        update_apply_status(false);

    // Grab the lock for the Print / PrintObject milestones.
    tbb::mutex::scoped_lock lock(this->state_mutex());

    // The following call may stop the background processing.
    if (! print_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(print_diff));
    if (! printer_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(printer_diff));
    if (! material_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(material_diff));

    // Apply variables to placeholder parser. The placeholder parser is currently used
    // only to generate the output file name.
	if (! placeholder_parser_diff.empty()) {
        // update_apply_status(this->invalidate_step(slapsRasterize));
		PlaceholderParser &pp = this->placeholder_parser();
		pp.apply_config(config);
        // Set the profile aliases for the PrintBase::output_filename()
		pp.set("print_preset", config_in.option("sla_print_settings_id")->clone());
		pp.set("material_preset", config_in.option("sla_material_settings_id")->clone());
		pp.set("printer_preset", config_in.option("printer_settings_id")->clone());
    }

    // It is also safe to change m_config now after this->invalidate_state_by_config_options() call.
    m_print_config.apply_only(config, print_diff, true);
    m_printer_config.apply_only(config, printer_diff, true);
    // Handle changes to material config.
    m_material_config.apply_only(config, material_diff, true);
    // Handle changes to object config defaults
    m_default_object_config.apply_only(config, object_diff, true);

    struct ModelObjectStatus {
        enum Status {
            Unknown,
            Old,
            New,
            Moved,
            Deleted,
        };
        ModelObjectStatus(ModelID id, Status status = Unknown) : id(id), status(status) {}
        ModelID                 id;
        Status                  status;
        // Search by id.
        bool operator<(const ModelObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::set<ModelObjectStatus> model_object_status;

    // 1) Synchronize model objects.
    if (model.id() != m_model.id()) {
        // Kill everything, initialize from scratch.
        // Stop background processing.
        this->call_cancell_callback();
        update_apply_status(this->invalidate_all_steps());
        for (SLAPrintObject *object : m_objects) {
            model_object_status.emplace(object->model_object()->id(), ModelObjectStatus::Deleted);
            delete object;
        }
        m_objects.clear();
        m_model.assign_copy(model);
        for (const ModelObject *model_object : m_model.objects)
            model_object_status.emplace(model_object->id(), ModelObjectStatus::New);
    } else {
        if (model_object_list_equal(m_model, model)) {
            // The object list did not change.
            for (const ModelObject *model_object : m_model.objects)
                model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
        } else if (model_object_list_extended(m_model, model)) {
            // Add new objects. Their volumes and configs will be synchronized later.
            update_apply_status(this->invalidate_step(slapsRasterize));
            for (const ModelObject *model_object : m_model.objects)
                model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
            for (size_t i = m_model.objects.size(); i < model.objects.size(); ++ i) {
                model_object_status.emplace(model.objects[i]->id(), ModelObjectStatus::New);
                m_model.objects.emplace_back(ModelObject::new_copy(*model.objects[i]));
                m_model.objects.back()->set_model(&m_model);
            }
        } else {
            // Reorder the objects, add new objects.
            // First stop background processing before shuffling or deleting the PrintObjects in the object list.
            this->call_cancell_callback();
            update_apply_status(this->invalidate_step(slapsRasterize));
            // Second create a new list of objects.
            std::vector<ModelObject*> model_objects_old(std::move(m_model.objects));
            m_model.objects.clear();
            m_model.objects.reserve(model.objects.size());
            auto by_id_lower = [](const ModelObject *lhs, const ModelObject *rhs){ return lhs->id() < rhs->id(); };
            std::sort(model_objects_old.begin(), model_objects_old.end(), by_id_lower);
            for (const ModelObject *mobj : model.objects) {
                auto it = std::lower_bound(model_objects_old.begin(), model_objects_old.end(), mobj, by_id_lower);
                if (it == model_objects_old.end() || (*it)->id() != mobj->id()) {
                    // New ModelObject added.
                    m_model.objects.emplace_back(ModelObject::new_copy(*mobj));
                    m_model.objects.back()->set_model(&m_model);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::New);
                } else {
                    // Existing ModelObject re-added (possibly moved in the list).
                    m_model.objects.emplace_back(*it);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::Moved);
                }
            }
            bool deleted_any = false;
            for (ModelObject *&model_object : model_objects_old) {
                if (model_object_status.find(ModelObjectStatus(model_object->id())) == model_object_status.end()) {
                    model_object_status.emplace(model_object->id(), ModelObjectStatus::Deleted);
                    deleted_any = true;
                } else
                    // Do not delete this ModelObject instance.
                    model_object = nullptr;
            }
            if (deleted_any) {
                // Delete PrintObjects of the deleted ModelObjects.
                std::vector<SLAPrintObject*> print_objects_old = std::move(m_objects);
                m_objects.clear();
                m_objects.reserve(print_objects_old.size());
                for (SLAPrintObject *print_object : print_objects_old) {
                    auto it_status = model_object_status.find(ModelObjectStatus(print_object->model_object()->id()));
                    assert(it_status != model_object_status.end());
                    if (it_status->status == ModelObjectStatus::Deleted) {
                        update_apply_status(print_object->invalidate_all_steps());
                        delete print_object;
                    } else
                        m_objects.emplace_back(print_object);
                }
                for (ModelObject *model_object : model_objects_old)
                    delete model_object;
            }
        }
    }

    // 2) Map print objects including their transformation matrices.
    struct PrintObjectStatus {
        enum Status {
            Unknown,
            Deleted,
            Reused,
            New
        };
        PrintObjectStatus(SLAPrintObject *print_object, Status status = Unknown) :
            id(print_object->model_object()->id()),
            print_object(print_object),
            trafo(print_object->trafo()),
            status(status) {}
        PrintObjectStatus(ModelID id) : id(id), print_object(nullptr), trafo(Transform3d::Identity()), status(Unknown) {}
        // ID of the ModelObject & PrintObject
        ModelID          id;
        // Pointer to the old PrintObject
        SLAPrintObject  *print_object;
        // Trafo generated with model_object->world_matrix(true)
        Transform3d      trafo;
        Status           status;
        // Search by id.
        bool operator<(const PrintObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::multiset<PrintObjectStatus> print_object_status;
    for (SLAPrintObject *print_object : m_objects)
        print_object_status.emplace(PrintObjectStatus(print_object));

    // 3) Synchronize ModelObjects & PrintObjects.
    std::vector<SLAPrintObject*> print_objects_new;
    print_objects_new.reserve(std::max(m_objects.size(), m_model.objects.size()));
    bool new_objects = false;
    for (size_t idx_model_object = 0; idx_model_object < model.objects.size(); ++ idx_model_object) {
        ModelObject &model_object = *m_model.objects[idx_model_object];
        auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
        assert(it_status != model_object_status.end());
        assert(it_status->status != ModelObjectStatus::Deleted);
        if (it_status->status == ModelObjectStatus::New)
            // PrintObject instances will be added in the next loop.
            continue;
        // Update the ModelObject instance, possibly invalidate the linked PrintObjects.
        assert(it_status->status == ModelObjectStatus::Old || it_status->status == ModelObjectStatus::Moved);
        const ModelObject &model_object_new       = *model.objects[idx_model_object];
        auto               it_print_object_status = print_object_status.lower_bound(PrintObjectStatus(model_object.id()));
        if (it_print_object_status != print_object_status.end() && it_print_object_status->id != model_object.id())
            it_print_object_status = print_object_status.end();
        // Check whether a model part volume was added or removed, their transformations or order changed.
        bool model_parts_differ = model_volume_list_changed(model_object, model_object_new, ModelVolume::MODEL_PART);
        bool sla_trafo_differs  = model_object.instances.empty() != model_object_new.instances.empty() ||
            (! model_object.instances.empty() && ! sla_trafo(model_object).isApprox(sla_trafo(model_object_new)));
        if (model_parts_differ || sla_trafo_differs) {
            // The very first step (the slicing step) is invalidated. One may freely remove all associated PrintObjects.
            if (it_print_object_status != print_object_status.end()) {
                update_apply_status(it_print_object_status->print_object->invalidate_all_steps());
                const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Deleted;
            }
            // Copy content of the ModelObject including its ID, do not change the parent.
            model_object.assign_copy(model_object_new);
        } else {
            // Synchronize Object's config.
            bool object_config_changed = model_object.config != model_object_new.config;
            if (object_config_changed)
                model_object.config = model_object_new.config;
            if (! object_diff.empty() || object_config_changed) {
                SLAPrintObjectConfig new_config = m_default_object_config;
                normalize_and_apply_config(new_config, model_object.config);
                if (it_print_object_status != print_object_status.end()) {
                    t_config_option_keys diff = it_print_object_status->print_object->config().diff(new_config);
                    if (! diff.empty()) {
                        update_apply_status(it_print_object_status->print_object->invalidate_state_by_config_options(diff));
                        it_print_object_status->print_object->config_apply_only(new_config, diff, true);
                    }
                }
            }
            if (model_object.sla_support_points != model_object_new.sla_support_points) {
                model_object.sla_support_points = model_object_new.sla_support_points;
                if (it_print_object_status != print_object_status.end())
                    update_apply_status(it_print_object_status->print_object->invalidate_step(slaposSupportPoints));
            }
            // Copy the ModelObject name, input_file and instances. The instances will compared against PrintObject instances in the next step.
            model_object.name       = model_object_new.name;
            model_object.input_file = model_object_new.input_file;
            model_object.clear_instances();
            model_object.instances.reserve(model_object_new.instances.size());
            for (const ModelInstance *model_instance : model_object_new.instances) {
                model_object.instances.emplace_back(new ModelInstance(*model_instance));
                model_object.instances.back()->set_model_object(&model_object);
            }
        }

        std::vector<SLAPrintObject::Instance> new_instances = sla_instances(model_object);
        if (it_print_object_status != print_object_status.end() && it_print_object_status->status != PrintObjectStatus::Deleted) {
            // The SLAPrintObject is already there.
            if (new_instances != it_print_object_status->print_object->instances()) {
                // Instances changed.
                it_print_object_status->print_object->set_instances(new_instances);
                update_apply_status(this->invalidate_step(slapsRasterize));
            }
            print_objects_new.emplace_back(it_print_object_status->print_object);
            const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Reused;
        } else {
            auto print_object = new SLAPrintObject(this, &model_object);
            print_object->set_trafo(sla_trafo(model_object));
            print_object->set_instances(new_instances);
            print_object->config_apply(config, true);
            print_objects_new.emplace_back(print_object);
            new_objects = true;
        }
    }

    if (m_objects != print_objects_new) {
        this->call_cancell_callback();
        update_apply_status(this->invalidate_all_steps());
        m_objects = print_objects_new;
        // Delete the PrintObjects marked as Unknown or Deleted.
        bool deleted_objects = false;
        for (auto &pos : print_object_status)
            if (pos.status == PrintObjectStatus::Unknown || pos.status == PrintObjectStatus::Deleted) {
                // update_apply_status(pos.print_object->invalidate_all_steps());
                delete pos.print_object;
                deleted_objects = true;
            }
        update_apply_status(new_objects);
    }

    this->update_object_placeholders();

#ifdef _DEBUG
    check_model_ids_equal(m_model, model);
#endif /* _DEBUG */

    return static_cast<ApplyStatus>(apply_status);
}

namespace {
// Compile the argument for support creation from the static print config.
sla::SupportConfig make_support_cfg(const SLAPrintObjectConfig& c) {
    sla::SupportConfig scfg;

    scfg.head_front_radius_mm = 0.5*c.support_head_front_diameter.getFloat();
    scfg.head_back_radius_mm = 0.5*c.support_pillar_diameter.getFloat();
    scfg.head_penetration_mm = c.support_head_penetration.getFloat();
    scfg.head_width_mm = c.support_head_width.getFloat();
    scfg.object_elevation_mm = c.support_object_elevation.getFloat();
    scfg.tilt = c.support_critical_angle.getFloat() * PI / 180.0 ;
    scfg.max_bridge_length_mm = c.support_max_bridge_length.getFloat();
    scfg.headless_pillar_radius_mm = 0.375*c.support_pillar_diameter.getFloat();
    scfg.pillar_widening_factor = c.support_pillar_widening_factor.getFloat();
    scfg.base_radius_mm = 0.5*c.support_base_diameter.getFloat();
    scfg.base_height_mm = c.support_base_height.getFloat();

    return scfg;
}

void swapXY(ExPolygon& expoly) {
    for(auto& p : expoly.contour.points) std::swap(p(X), p(Y));
    for(auto& h : expoly.holes) for(auto& p : h.points) std::swap(p(X), p(Y));
}

}

void SLAPrint::process()
{
    using namespace sla;

    // Assumption: at this point the print objects should be populated only with
    // the model objects we have to process and the instances are also filtered

    // shortcut to initial layer height
    double ilhd = m_material_config.initial_layer_height.getFloat();
    auto   ilh  = float(ilhd);
    const size_t objcount = m_objects.size();

    const unsigned min_objstatus = 0;   // where the per object operations start
    const unsigned max_objstatus = 80;  // where the per object operations end

    // the coefficient that multiplies the per object status values which
    // are set up for <0, 100>. They need to be scaled into the whole process
    const double ostepd = (max_objstatus - min_objstatus) / (objcount * 100.0);

    // The slicing will be performed on an imaginary 1D grid which starts from
    // the bottom of the bounding box created around the supported model. So
    // the first layer which is usually thicker will be part of the supports
    // not the model geometry. Exception is when the model is not in the air
    // (elevation is zero) and no pad creation was requested. In this case the
    // model geometry starts on the ground level and the initial layer is part
    // of it. In any case, the model and the supports have to be sliced in the
    // same imaginary grid (the height vector argument to TriangleMeshSlicer).

    // Slicing the model object. This method is oversimplified and needs to
    // be compared with the fff slicing algorithm for verification
    auto slice_model = [this, ilh](SLAPrintObject& po) {
        double lh = po.m_config.layer_height.getFloat();

        TriangleMesh mesh = po.transformed_mesh();
        TriangleMeshSlicer slicer(&mesh);
        auto bb3d = mesh.bounding_box();

        double elevation = po.get_elevation();

        float minZ = float(bb3d.min(Z)) - float(elevation);
        float maxZ = float(bb3d.max(Z)) ;
        auto flh = float(lh);
        auto gnd = float(bb3d.min(Z));

        // The 1D grid heights
        std::vector<float> heights;

        // The first layer (the one before the initial height) is added only
        // if there is no pad and no elevation value
        if(minZ >= gnd) heights.emplace_back(minZ);

        for(float h = minZ + ilh; h < maxZ; h += flh)
            if(h >= gnd) heights.emplace_back(h);

        auto& layers = po.m_model_slices; layers.clear();
        slicer.slice(heights, &layers, [this](){ throw_if_canceled(); });
    };

    // this procedure simply converts the points and copies them into
    // the support data cache
    auto support_points = [](SLAPrintObject& po) {
        ModelObject& mo = *po.m_model_object;
        po.m_supportdata.reset(new SLAPrintObject::SupportData());

        if(!mo.sla_support_points.empty()) {
            po.m_supportdata->emesh = sla::to_eigenmesh(po.transformed_mesh());
            po.m_supportdata->support_points =
                    sla::to_point_set(po.transformed_support_points());
        } else if(po.m_config.supports_enable.getBool()) {
            // Supports are enabled but there are no support points to process.
            // We throw here a runtime exception with some explanation and
            // the background processing framework will handle it.
            throw std::runtime_error(
                L("Supports are enabled but no support points selected."
                  " Hint: create some support points or disable support "
                  "creation."));
        }
    };

    // In this step we create the supports
    auto support_tree = [this, objcount, ostepd](SLAPrintObject& po) {
        if(!po.m_supportdata) return;

        if(!po.m_config.supports_enable.getBool()) {
            // Generate empty support tree. It can still host a pad
            po.m_supportdata->support_tree_ptr.reset(new SLASupportTree());
            return;
        }

        auto& emesh = po.m_supportdata->emesh;
        auto& pts = po.m_supportdata->support_points; // nowhere filled yet
        try {
            sla::SupportConfig scfg = make_support_cfg(po.m_config);
            sla::Controller ctl;

            // some magic to scale the status values coming from the support
            // tree creation into the whole print process
            auto stfirst = OBJ_STEP_LEVELS.begin();
            auto stthis = stfirst + slaposSupportTree;
            // we need to add up the status portions until this operation
            int init = std::accumulate(stfirst, stthis, 0);
            init = int(init * ostepd);     // scale the init portion

            // scaling for the sub operations
            double d = *stthis / (objcount * 100.0);

            ctl.statuscb = [this, init, d](unsigned st, const std::string& msg){
                set_status(int(init + st*d), msg);
            };
            ctl.stopcondition = [this](){ return canceled(); };
            ctl.cancelfn = [this]() { throw_if_canceled(); };

            po.m_supportdata->support_tree_ptr.reset(
                        new SLASupportTree(pts, emesh, scfg, ctl));

            // Create the unified mesh
            auto rc = SlicingStatus::RELOAD_SCENE;
            set_status(-1, L("Visualizing supports"));
            po.m_supportdata->support_tree_ptr->merged_mesh();
            set_status(-1, L("Visualizing supports"), rc);
        } catch(sla::SLASupportsStoppedException&) {
            // no need to rethrow
            // throw_if_canceled();
        }
    };

    // This step generates the sla base pad
    auto base_pool = [this](SLAPrintObject& po) {
        // this step can only go after the support tree has been created
        // and before the supports had been sliced. (or the slicing has to be
        // repeated)

        if(po.m_config.pad_enable.getBool() &&
           po.m_supportdata &&
           po.m_supportdata->support_tree_ptr)
        {
            double wt = po.m_config.pad_wall_thickness.getFloat();
            double h =  po.m_config.pad_wall_height.getFloat();
            double md = po.m_config.pad_max_merge_distance.getFloat();
            double er = po.m_config.pad_edge_radius.getFloat();
            double lh = po.m_config.layer_height.getFloat();
            double elevation = po.m_config.support_object_elevation.getFloat();
            if(!po.m_config.supports_enable.getBool()) elevation = 0;
            sla::PoolConfig pcfg(wt, h, md, er);

            sla::ExPolygons bp;
            double pad_h = sla::get_pad_elevation(pcfg);
            auto&& trmesh = po.transformed_mesh();

            // This call can get pretty time consuming
            auto thrfn = [this](){ throw_if_canceled(); };

            if(elevation < pad_h)
                sla::base_plate(trmesh, bp, float(pad_h), float(lh),
                                            thrfn);

            pcfg.throw_on_cancel = thrfn;
            po.m_supportdata->support_tree_ptr->add_pad(bp, pcfg);
        }

//        // if the base pool (which means also the support tree) is
//        // done, do a refresh when indicating progress. Now the
//        // geometries for the supports and the optional base pad are
//        // ready. We can grant access for the control thread to read
//        // the geometries, but first we have to update the caches:
//        po.support_mesh(); /*po->pad_mesh();*/
        po.throw_if_canceled();
        auto rc = SlicingStatus::RELOAD_SCENE;
        set_status(-1, L("Visualizing supports"), rc);
    };

    // Slicing the support geometries similarly to the model slicing procedure.
    // If the pad had been added previously (see step "base_pool" than it will
    // be part of the slices)
    auto slice_supports = [ilh](SLAPrintObject& po) {
        auto& sd = po.m_supportdata;
        if(sd && sd->support_tree_ptr) {
            auto lh = float(po.m_config.layer_height.getFloat());
            sd->support_slices = sd->support_tree_ptr->slice(lh, ilh);
        }
    };

    // We have the layer polygon collection but we need to unite them into
    // an index where the key is the height level in discrete levels (clipper)
    auto index_slices = [ilhd](SLAPrintObject& po) {
        po.m_slice_index.clear();
        auto sih = LevelID(scale_(ilhd));

        // For all print objects, go through its initial layers and place them
        // into the layers hash
        auto bb = po.transformed_mesh().bounding_box();
        double modelgnd = bb.min(Z);
        double elevation = po.get_elevation();
        double lh = po.m_config.layer_height.getFloat();
        double minZ = modelgnd - elevation;

        // scaled values:
        auto sminZ = LevelID(scale_(minZ));
        auto smaxZ = LevelID(scale_(bb.max(Z)));
        auto smodelgnd = LevelID(scale_(modelgnd));
        auto slh = LevelID(scale_(lh));

        // It is important that the next levels match the levels in
        // model_slice method. Only difference is that here it works with
        // scaled coordinates
        auto& levelids = po.m_level_ids; levelids.clear();
        if(sminZ >= smodelgnd) levelids.emplace_back(sminZ);
        for(LevelID h = sminZ + sih; h < smaxZ; h += slh)
            if(h >= smodelgnd) levelids.emplace_back(h);

        SlicedModel & oslices = po.m_model_slices;

        // If everything went well this code should not run at all, but
        // let's be robust...
        // assert(levelids.size() == oslices.size());
        if(levelids.size() < oslices.size()) { // extend the levels until...

            BOOST_LOG_TRIVIAL(warning)
                    << "Height level mismatch at rasterization!\n";

            LevelID lastlvl = levelids.back();
            while(levelids.size() < oslices.size()) {
                lastlvl += slh;
                levelids.emplace_back(lastlvl);
            }
        }

        // shortcut for empty index into the slice vectors
        static const auto EMPTY_SLICE = SLAPrintObject::SliceRecord::NONE;
        
        for(size_t i = 0; i < oslices.size(); ++i) {
            LevelID h = levelids[i];

            float fh = float(double(h) * SCALING_FACTOR);

            // now for the public slice index:
            SLAPrintObject::SliceRecord& sr = po.m_slice_index[fh];
            // There should be only one slice layer for each print object
            assert(sr.model_slices_idx == EMPTY_SLICE);
            sr.model_slices_idx = i;
        }

        if(po.m_supportdata) { // deal with the support slices if present
            auto& sslices = po.m_supportdata->support_slices;
            po.m_supportdata->level_ids.clear();
            po.m_supportdata->level_ids.reserve(sslices.size());

            for(int i = 0; i < int(sslices.size()); ++i) {
                int a = i == 0 ? 0 : 1;
                int b = i == 0 ? 0 : i - 1;
                LevelID h = sminZ + a * sih + b * slh;
                po.m_supportdata->level_ids.emplace_back(h);

                float fh = float(double(h) * SCALING_FACTOR);

                SLAPrintObject::SliceRecord& sr = po.m_slice_index[fh];
                assert(sr.support_slices_idx == EMPTY_SLICE);
                sr.support_slices_idx = SLAPrintObject::SliceRecord::Idx(i);
            }
        }
    };

    auto& levels = m_printer_input;

    // Rasterizing the model objects, and their supports
    auto rasterize = [this, max_objstatus, &levels]() {
        if(canceled()) return;

        // clear the rasterizer input
        m_printer_input.clear();

        for(SLAPrintObject * o : m_objects) {
            auto& po = *o;
            SlicedModel & oslices = po.m_model_slices;

            // We need to adjust the min Z level of the slices to be zero
            LevelID smfirst = po.m_supportdata? po.m_supportdata->level_ids.front() : 0;
            LevelID mfirst = po.m_level_ids.front();
            LevelID gndlvl = -(std::min(smfirst, mfirst));

            // now merge this object's support and object slices with the rest
            // of the print object slices

            for(size_t i = 0; i < oslices.size(); ++i) {
                auto& lyrs = levels[gndlvl + po.m_level_ids[i]];
                lyrs.emplace_back(oslices[i], po.m_instances);
            }

            if(!po.m_supportdata) continue;
            auto& sslices = po.m_supportdata->support_slices;
            for(size_t i = 0; i < sslices.size(); ++i) {
                auto& lyrs = levels[gndlvl + po.m_supportdata->level_ids[i]];
                lyrs.emplace_back(sslices[i], po.m_instances);
            }
        }

        // collect all the keys
        std::vector<long long> keys; keys.reserve(levels.size());
        for(auto& e : levels) keys.emplace_back(e.first);

        // If the raster has vertical orientation, we will flip the coordinates
        bool flpXY = m_printer_config.display_orientation.getInt() ==
                SLADisplayOrientation::sladoPortrait;

        { // create a raster printer for the current print parameters
            // I don't know any better
            auto& ocfg = m_objects.front()->m_config;
            auto& matcfg = m_material_config;
            auto& printcfg = m_printer_config;

            double w = printcfg.display_width.getFloat();
            double h = printcfg.display_height.getFloat();
            auto pw = unsigned(printcfg.display_pixels_x.getInt());
            auto ph = unsigned(printcfg.display_pixels_y.getInt());
            double lh = ocfg.layer_height.getFloat();
            double exp_t = matcfg.exposure_time.getFloat();
            double iexp_t = matcfg.initial_exposure_time.getFloat();

            if(flpXY) { std::swap(w, h); std::swap(pw, ph); }

            m_printer.reset(new SLAPrinter(w, h, pw, ph, lh, exp_t, iexp_t,
                                           flpXY? SLAPrinter::RO_PORTRAIT :
                                                  SLAPrinter::RO_LANDSCAPE));
        }

        // Allocate space for all the layers
        SLAPrinter& printer = *m_printer;
        auto lvlcnt = unsigned(levels.size());
        printer.layers(lvlcnt);

        unsigned slot = PRINT_STEP_LEVELS[slapsRasterize];
        unsigned ist = max_objstatus, pst = ist;
        double sd = (100 - ist) / 100.0;
        SpinMutex slck;

        // procedure to process one height level. This will run in parallel
        auto lvlfn =
        [this, &slck, &keys, &levels, &printer, slot, sd, ist, &pst, flpXY]
            (unsigned level_id)
        {
            if(canceled()) return;

            LayerRefs& lrange = levels[keys[level_id]];

            // Switch to the appropriate layer in the printer
            printer.begin_layer(level_id);

            for(auto& lyrref : lrange) { // for all layers in the current level
                if(canceled()) break;
                const Layer& sl = lyrref.lref;   // get the layer reference
                const LayerCopies& copies = lyrref.copies;

                // Draw all the polygons in the slice to the actual layer.
                for(auto& cp : copies) {
                    for(ExPolygon slice : sl) {
                        // The order is important here:
                        // apply rotation before translation...
                        slice.rotate(double(cp.rotation));
                        slice.translate(cp.shift(X), cp.shift(Y));
                        if(flpXY) swapXY(slice);
                        printer.draw_polygon(slice, level_id);
                    }
                }
            }

            // Finish the layer for later saving it.
            printer.finish_layer(level_id);

            // Status indication
            auto st = ist + unsigned(sd*level_id*slot/levels.size());
            { std::lock_guard<SpinMutex> lck(slck);
            if( st > pst) {
                set_status(int(st), PRINT_STEP_LABELS[slapsRasterize]);
                pst = st;
            }
            }
        };

        // last minute escape
        if(canceled()) return;

        // Sequential version (for testing)
        // for(unsigned l = 0; l < lvlcnt; ++l) process_level(l);

        // Print all the layers in parallel
        tbb::parallel_for<unsigned, decltype(lvlfn)>(0, lvlcnt, lvlfn);
    };

    using slaposFn = std::function<void(SLAPrintObject&)>;
    using slapsFn  = std::function<void(void)>;

    // This is the actual order of steps done on each PrintObject
    std::array<SLAPrintObjectStep, slaposCount> objectsteps = {
        slaposObjectSlice,      // Support Islands will need this step
        slaposSupportIslands,
        slaposSupportPoints,
        slaposSupportTree,
        slaposBasePool,
        slaposSliceSupports,
        slaposIndexSlices
    };

    std::array<slaposFn, slaposCount> pobj_program =
    {
        slice_model,
        [](SLAPrintObject&){}, // slaposSupportIslands now empty
        support_points,
        support_tree,
        base_pool,
        slice_supports,
        index_slices
    };

    std::array<slapsFn, slapsCount> print_program =
    {
        rasterize,
        [](){}  // validate
    };

    unsigned st = min_objstatus;
    unsigned incr = 0;

    // TODO: this loop could run in parallel but should not exhaust all the CPU
    // power available
    for(SLAPrintObject * po : m_objects) {
        for(size_t s = 0; s < objectsteps.size(); ++s) {
            auto currentstep = objectsteps[s];

            // Cancellation checking. Each step will check for cancellation
            // on its own and return earlier gracefully. Just after it returns
            // execution gets to this point and throws the canceled signal.
            throw_if_canceled();

            st += unsigned(incr * ostepd);

            if(po->m_stepmask[currentstep] && po->set_started(currentstep)) {

                set_status(int(st), OBJ_STEP_LABELS[currentstep]);
                pobj_program[currentstep](*po);
                po->set_done(currentstep);
            }

            incr = OBJ_STEP_LEVELS[currentstep];
        }
    }

    std::array<SLAPrintStep, slapsCount> printsteps = {
        slapsRasterize, slapsValidate
    };

    // this would disable the rasterization step
//    m_stepmask[slapsRasterize] = false;

    double pstd = (100 - max_objstatus) / 100.0;
    st = max_objstatus;
    for(size_t s = 0; s < print_program.size(); ++s) {
        auto currentstep = printsteps[s];

        throw_if_canceled();

        if(m_stepmask[currentstep] && set_started(currentstep))
        {
            set_status(int(st), PRINT_STEP_LABELS[currentstep]);
            print_program[currentstep]();
            set_done(currentstep);
        }

        st += unsigned(PRINT_STEP_LEVELS[currentstep] * pstd);
    }

    // If everything vent well
    set_status(100, L("Slicing done"));
}

bool SLAPrint::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    // Cache the plenty of parameters, which influence the final rasterization only,
    // or they are only notes not influencing the rasterization step.
    static std::unordered_set<std::string> steps_rasterize = {
        "exposure_time",
        "initial_exposure_time",
        "material_correction_printing",
        "material_correction_curing",
        "display_width",
        "display_height",
        "display_pixels_x",
        "display_pixels_y",
        "printer_correction"
    };

    static std::unordered_set<std::string> steps_ignore = {
        "bed_shape",
        "max_print_height",
        "printer_technology",
        "output_filename_format"
    };

    std::vector<SLAPrintStep> steps;
    std::vector<SLAPrintObjectStep> osteps;
    bool invalidated = false;

    for (const t_config_option_key &opt_key : opt_keys) {
        if (steps_rasterize.find(opt_key) != steps_rasterize.end()) {
            // These options only affect the final rasterization, or they are just notes without influence on the output,
            // so there is nothing to invalidate.
            steps.emplace_back(slapsRasterize);
        } else if (steps_ignore.find(opt_key) != steps_ignore.end()) {
            // These steps have no influence on the output. Just ignore them.
        } else if (opt_key == "initial_layer_height") {
            steps.emplace_back(slapsRasterize);
            osteps.emplace_back(slaposObjectSlice);
        } else {
            // All values should be covered.
            assert(false);
        }
    }

    sort_remove_duplicates(steps);
    for (SLAPrintStep step : steps)
        invalidated |= this->invalidate_step(step);
    sort_remove_duplicates(osteps);
    for (SLAPrintObjectStep ostep : osteps)
        for (SLAPrintObject *object : m_objects)
            invalidated |= object->invalidate_step(ostep);
    return invalidated;
}

// Returns true if an object step is done on all objects and there's at least one object.
bool SLAPrint::is_step_done(SLAPrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    tbb::mutex::scoped_lock lock(this->state_mutex());
    for (const SLAPrintObject *object : m_objects)
        if (! object->m_state.is_done_unguarded(step))
            return false;
    return true;
}

SLAPrintObject::SLAPrintObject(SLAPrint *print, ModelObject *model_object):
    Inherited(print, model_object),
    m_stepmask(slaposCount, true),
    m_transformed_rmesh( [this](TriangleMesh& obj){
            obj = m_model_object->raw_mesh(); obj.transform(m_trafo);
        })
{
}

SLAPrintObject::~SLAPrintObject() {}

// Called by SLAPrint::apply_config().
// This method only accepts SLAPrintObjectConfig option keys.
bool SLAPrintObject::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    std::vector<SLAPrintObjectStep> steps;
    bool invalidated = false;
    for (const t_config_option_key &opt_key : opt_keys) {
		if (opt_key == "layer_height") {
			steps.emplace_back(slaposObjectSlice);
        } else if (opt_key == "supports_enable") {
            steps.emplace_back(slaposSupportPoints);
		} else if (
               opt_key == "support_head_front_diameter"
            || opt_key == "support_head_penetration"
            || opt_key == "support_head_width"
            || opt_key == "support_pillar_diameter"
            || opt_key == "support_base_diameter"
            || opt_key == "support_base_height"
            || opt_key == "support_critical_angle"
            || opt_key == "support_max_bridge_length"
            || opt_key == "support_object_elevation") {
            steps.emplace_back(slaposSupportTree);
        } else if (
               opt_key == "pad_enable"
            || opt_key == "pad_wall_thickness"
            || opt_key == "pad_wall_height"
            || opt_key == "pad_max_merge_distance"
            || opt_key == "pad_edge_radius") {
            steps.emplace_back(slaposBasePool);
        } else {
            // All keys should be covered.
            assert(false);
        }
    }

    sort_remove_duplicates(steps);
    for (SLAPrintObjectStep step : steps)
        invalidated |= this->invalidate_step(step);
    return invalidated;
}

bool SLAPrintObject::invalidate_step(SLAPrintObjectStep step)
{
    bool invalidated = Inherited::invalidate_step(step);
    // propagate to dependent steps
    if (step == slaposObjectSlice) {
        invalidated |= this->invalidate_all_steps();
    } else if (step == slaposSupportIslands) {
        invalidated |= this->invalidate_steps({ slaposSupportPoints, slaposSupportTree, slaposBasePool, slaposSliceSupports, slaposIndexSlices });
        invalidated |= m_print->invalidate_step(slapsRasterize);
    } else if (step == slaposSupportPoints) {
        invalidated |= this->invalidate_steps({ slaposSupportTree, slaposBasePool, slaposSliceSupports, slaposIndexSlices });
        invalidated |= m_print->invalidate_step(slapsRasterize);
    } else if (step == slaposSupportTree) {
        invalidated |= this->invalidate_steps({ slaposBasePool, slaposSliceSupports, slaposIndexSlices });
        invalidated |= m_print->invalidate_step(slapsRasterize);
    } else if (step == slaposBasePool) {
        invalidated |= this->invalidate_steps({slaposSliceSupports, slaposIndexSlices});
        invalidated |= m_print->invalidate_step(slapsRasterize);
    } else if (step == slaposSliceSupports) {
        invalidated |= this->invalidate_step(slaposIndexSlices);
        invalidated |= m_print->invalidate_step(slapsRasterize);
    } else if(step == slaposIndexSlices) {
        invalidated |= m_print->invalidate_step(slapsRasterize);
    }
    return invalidated;
}

bool SLAPrintObject::invalidate_all_steps()
{
    return Inherited::invalidate_all_steps() | m_print->invalidate_all_steps();
}

double SLAPrintObject::get_elevation() const {
    bool se = m_config.supports_enable.getBool();
    double ret = se? m_config.support_object_elevation.getFloat() : 0;

    // if the pad is enabled, then half of the pad height is its base plate
    if(m_config.pad_enable.getBool()) {
        // Normally the elevation for the pad itself would be the thickness of
        // its walls but currently it is half of its thickness. Whatever it
        // will be in the future, we provide the config to the get_pad_elevation
        // method and we will have the correct value
        sla::PoolConfig pcfg;
        pcfg.min_wall_height_mm = m_config.pad_wall_height.getFloat();
        pcfg.min_wall_thickness_mm = m_config.pad_wall_thickness.getFloat();
        pcfg.edge_radius_mm = m_config.pad_edge_radius.getFloat();
        pcfg.max_merge_distance_mm = m_config.pad_max_merge_distance.getFloat();
        ret += sla::get_pad_elevation(pcfg);
    }

    return ret;
}

double SLAPrintObject::get_current_elevation() const
{
    bool se = m_config.supports_enable.getBool();
    bool has_supports = is_step_done(slaposSupportTree);
    bool has_pad = is_step_done(slaposBasePool);
    if(!has_supports && !has_pad) return 0;
    else if(has_supports && !has_pad)
        return se ? m_config.support_object_elevation.getFloat() : 0;
    else return get_elevation();

    return 0;
}

namespace { // dummy empty static containers for return values in some methods
const std::vector<ExPolygons> EMPTY_SLICES;
const TriangleMesh EMPTY_MESH;
}

const std::vector<ExPolygons> &SLAPrintObject::get_support_slices() const
{
    // assert(is_step_done(slaposSliceSupports));
    if (!m_supportdata) return EMPTY_SLICES;
    return m_supportdata->support_slices;
}

const SLAPrintObject::SliceIndex &SLAPrintObject::get_slice_index() const
{
    // assert(is_step_done(slaposIndexSlices));
    return m_slice_index;
}

const std::vector<ExPolygons> &SLAPrintObject::get_model_slices() const
{
    // assert(is_step_done(slaposObjectSlice));
    return m_model_slices;
}

bool SLAPrintObject::has_mesh(SLAPrintObjectStep step) const
{
    switch (step) {
    case slaposSupportTree:
        return ! this->support_mesh().empty();
    case slaposBasePool:
        return ! this->pad_mesh().empty();
    default:
        return false;
    }
}

TriangleMesh SLAPrintObject::get_mesh(SLAPrintObjectStep step) const
{
    switch (step) {
    case slaposSupportTree:
        return this->support_mesh();
    case slaposBasePool:
        return this->pad_mesh();
    default:
        return TriangleMesh();
    }
}

const TriangleMesh& SLAPrintObject::support_mesh() const
{
    if(m_config.supports_enable.getBool() && m_supportdata &&
       m_supportdata->support_tree_ptr) {
        return m_supportdata->support_tree_ptr->merged_mesh();
    }

    return EMPTY_MESH;
}

const TriangleMesh& SLAPrintObject::pad_mesh() const
{
    if(m_config.pad_enable.getBool() && m_supportdata && m_supportdata->support_tree_ptr)
        return m_supportdata->support_tree_ptr->get_pad();

    return EMPTY_MESH;
}

const TriangleMesh &SLAPrintObject::transformed_mesh() const {
    // we need to transform the raw mesh...
    // currently all the instances share the same x and y rotation and scaling
    // so we have to extract those from e.g. the first instance and apply to the
    // raw mesh. This is also true for the support points.
    // BUT: when the support structure is spawned for each instance than it has
    // to omit the X, Y rotation and scaling as those have been already applied
    // or apply an inverse transformation on the support structure after it
    // has been created.

    return m_transformed_rmesh.get();
}

std::vector<Vec3d> SLAPrintObject::transformed_support_points() const
{
    assert(m_model_object != nullptr);
    auto& spts = m_model_object->sla_support_points;

    // this could be cached as well
    std::vector<Vec3d> ret; ret.reserve(spts.size());

    for(auto& sp : spts) ret.emplace_back( trafo() * Vec3d(sp.cast<double>()));

    return ret;
}

} // namespace Slic3r
