#include "SLAPrint.hpp"
#include "SLA/SLASupportTree.hpp"
#include "SLA/SLABasePool.hpp"
#include "SLA/SLAAutoSupports.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "MTUtils.hpp"

#include <unordered_set>
#include <numeric>

#include <tbb/parallel_for.h>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

// For geometry algorithms with native Clipper types (no copies and conversions)
#include <libnest2d/backends/clipper/geometries.hpp>

//#include <tbb/spin_mutex.h>//#include "tbb/mutex.h"

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

using SupportTreePtr = std::unique_ptr<sla::SLASupportTree>;

class SLAPrintObject::SupportData
{
public:
    sla::EigenMesh3D emesh;              // index-triangle representation
    std::vector<sla::SupportPoint> support_points;     // all the support points (manual/auto)
    SupportTreePtr                 support_tree_ptr;   // the supports
    std::vector<ExPolygons>        support_slices;     // sliced supports

    inline SupportData(const TriangleMesh &trmesh) : emesh(trmesh) {}
};

namespace {

// should add up to 100 (%)
const std::array<unsigned, slaposCount>     OBJ_STEP_LEVELS =
{
    30,     // slaposObjectSlice,
    20,     // slaposSupportPoints,
    10,     // slaposSupportTree,
    10,     // slaposBasePool,
    30,     // slaposSliceSupports,
};

// Object step to status label. The labels are localized at the time of calling, thus supporting language switching.
std::string OBJ_STEP_LABELS(size_t idx)
{
    switch (idx) {
    case slaposObjectSlice:     return L("Slicing model");
    case slaposSupportPoints:   return L("Generating support points");
    case slaposSupportTree:     return L("Generating support tree");
    case slaposBasePool:        return L("Generating pad");
    case slaposSliceSupports:   return L("Slicing supports");
    default:;
    }
    assert(false); return "Out of bounds!";
};

// Should also add up to 100 (%)
const std::array<unsigned, slapsCount> PRINT_STEP_LEVELS =
{
    10,      // slapsMergeSlicesAndEval
    90,      // slapsRasterize
};

// Print step to status label. The labels are localized at the time of calling, thus supporting language switching.
std::string PRINT_STEP_LABELS(size_t idx)
{
    switch (idx) {
    case slapsMergeSlicesAndEval:   return L("Merging slices and calculating statistics");
    case slapsRasterize:            return L("Rasterizing layers");
    default:;
    }
    assert(false); return "Out of bounds!";
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
static Transform3d sla_trafo(const SLAPrint& p, const ModelObject &model_object)
{

    Vec3d corr = p.relative_correction();

    ModelInstance &model_instance = *model_object.instances.front();
    Vec3d          offset         = model_instance.get_offset();
    Vec3d          rotation       = model_instance.get_rotation();
    offset(0) = 0.;
    offset(1) = 0.;
    rotation(2) = 0.;

    offset(Z) *= corr(Z);

    auto trafo = Transform3d::Identity();
    trafo.translate(offset);
    trafo.scale(corr);
    trafo.rotate(Eigen::AngleAxisd(rotation(2), Vec3d::UnitZ()));
    trafo.rotate(Eigen::AngleAxisd(rotation(1), Vec3d::UnitY()));
    trafo.rotate(Eigen::AngleAxisd(rotation(0), Vec3d::UnitX()));
    trafo.scale(model_instance.get_scaling_factor());
    trafo.scale(model_instance.get_mirror());

    if (model_instance.is_left_handed())
        trafo = Eigen::Scaling(Vec3d(-1., 1., 1.)) * trafo;

    return trafo;
}

// List of instances, where the ModelInstance transformation is a composite of sla_trafo and the transformation defined by SLAPrintObject::Instance.
static std::vector<SLAPrintObject::Instance> sla_instances(const ModelObject &model_object)
{
    std::vector<SLAPrintObject::Instance> instances;
    assert(! model_object.instances.empty());
    if (! model_object.instances.empty()) {
        Vec3d rotation0 = model_object.instances.front()->get_rotation();
        rotation0(2) = 0.;
        for (ModelInstance *model_instance : model_object.instances)
            if (model_instance->is_printable()) {
                instances.emplace_back(
                    model_instance->id(),
                    Point::new_scale(model_instance->get_offset(X), model_instance->get_offset(Y)),
                    float(Geometry::rotation_diff_z(rotation0, model_instance->get_rotation())));
            }
    }
    return instances;
}

SLAPrint::ApplyStatus SLAPrint::apply(const Model &model, DynamicPrintConfig config)
{
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    // Normalize the config.
    config.option("sla_print_settings_id",    true);
    config.option("sla_material_settings_id", true);
    config.option("printer_settings_id",      true);
    config.normalize();
    // Collect changes to print config.
    t_config_option_keys print_diff    = m_print_config.diff(config);
    t_config_option_keys printer_diff  = m_printer_config.diff(config);
    t_config_option_keys material_diff = m_material_config.diff(config);
    t_config_option_keys object_diff   = m_default_object_config.diff(config);
    t_config_option_keys placeholder_parser_diff = m_placeholder_parser.config_diff(config);

    // Do not use the ApplyStatus as we will use the max function when updating apply_status.
    unsigned int apply_status = APPLY_STATUS_UNCHANGED;
    auto update_apply_status = [&apply_status](bool invalidated)
        { apply_status = std::max<unsigned int>(apply_status, invalidated ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED); };
    if (! (print_diff.empty() && printer_diff.empty() && material_diff.empty() && object_diff.empty()))
        update_apply_status(false);

    // Grab the lock for the Print / PrintObject milestones.
    tbb::mutex::scoped_lock lock(this->state_mutex());

    // The following call may stop the background processing.
    bool invalidate_all_model_objects = false;
    if (! print_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(print_diff, invalidate_all_model_objects));
    if (! printer_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(printer_diff, invalidate_all_model_objects));
    if (! material_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(material_diff, invalidate_all_model_objects));

    // Apply variables to placeholder parser. The placeholder parser is currently used
    // only to generate the output file name.
    if (! placeholder_parser_diff.empty()) {
        // update_apply_status(this->invalidate_step(slapsRasterize));
        m_placeholder_parser.apply_config(config);
        // Set the profile aliases for the PrintBase::output_filename()
        m_placeholder_parser.set("print_preset",    config.option("sla_print_settings_id")->clone());
        m_placeholder_parser.set("material_preset", config.option("sla_material_settings_id")->clone());
        m_placeholder_parser.set("printer_preset",  config.option("printer_settings_id")->clone());
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
        ModelObjectStatus(ObjectID id, Status status = Unknown) : id(id), status(status) {}
        ObjectID                id;
        Status                  status;
        // Search by id.
        bool operator<(const ModelObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::set<ModelObjectStatus> model_object_status;

    // 1) Synchronize model objects.
    if (model.id() != m_model.id() || invalidate_all_model_objects) {
        // Kill everything, initialize from scratch.
        // Stop background processing.
        this->call_cancel_callback();
        update_apply_status(this->invalidate_all_steps());
        for (SLAPrintObject *object : m_objects) {
            model_object_status.emplace(object->model_object()->id(), ModelObjectStatus::Deleted);
            update_apply_status(object->invalidate_all_steps());
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
            update_apply_status(this->invalidate_step(slapsMergeSlicesAndEval));
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
            this->call_cancel_callback();
            update_apply_status(this->invalidate_step(slapsMergeSlicesAndEval));
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
        PrintObjectStatus(ObjectID id) : id(id), print_object(nullptr), trafo(Transform3d::Identity()), status(Unknown) {}
        // ID of the ModelObject & PrintObject
        ObjectID         id;
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
        // PrintObject for this ModelObject, if it exists.
        auto it_print_object_status = print_object_status.end();
        if (it_status->status != ModelObjectStatus::New) {
            // Update the ModelObject instance, possibly invalidate the linked PrintObjects.
            assert(it_status->status == ModelObjectStatus::Old || it_status->status == ModelObjectStatus::Moved);
            const ModelObject &model_object_new       = *model.objects[idx_model_object];
            it_print_object_status = print_object_status.lower_bound(PrintObjectStatus(model_object.id()));
            if (it_print_object_status != print_object_status.end() && it_print_object_status->id != model_object.id())
                it_print_object_status = print_object_status.end();
            // Check whether a model part volume was added or removed, their transformations or order changed.
            bool model_parts_differ = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::MODEL_PART);
            bool sla_trafo_differs  =
                model_object.instances.empty() != model_object_new.instances.empty() ||
                (! model_object.instances.empty() &&
                  (! sla_trafo(*this, model_object).isApprox(sla_trafo(*this, model_object_new)) ||
                    model_object.instances.front()->is_left_handed() != model_object_new.instances.front()->is_left_handed()));
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
                    static_cast<DynamicPrintConfig&>(model_object.config) = static_cast<const DynamicPrintConfig&>(model_object_new.config);
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

                bool old_user_modified = model_object.sla_points_status == sla::PointsStatus::UserModified;
                bool new_user_modified = model_object_new.sla_points_status == sla::PointsStatus::UserModified;
                if ((old_user_modified && ! new_user_modified) || // switching to automatic supports from manual supports
                    (! old_user_modified && new_user_modified) || // switching to manual supports from automatic supports
                    (new_user_modified && model_object.sla_support_points != model_object_new.sla_support_points)) {
                    if (it_print_object_status != print_object_status.end())
                        update_apply_status(it_print_object_status->print_object->invalidate_step(slaposSupportPoints));

                    model_object.sla_support_points = model_object_new.sla_support_points;
                }
                model_object.sla_points_status = model_object_new.sla_points_status;

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
        }

        std::vector<SLAPrintObject::Instance> new_instances = sla_instances(model_object);
        if (it_print_object_status != print_object_status.end() && it_print_object_status->status != PrintObjectStatus::Deleted) {
            // The SLAPrintObject is already there.
            if (new_instances.empty()) {
                const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Deleted;
            } else {
                if (new_instances != it_print_object_status->print_object->instances()) {
                    // Instances changed.
                    it_print_object_status->print_object->set_instances(new_instances);
                    update_apply_status(this->invalidate_step(slapsMergeSlicesAndEval));
                }
                print_objects_new.emplace_back(it_print_object_status->print_object);
                const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Reused;
            }
        } else if (! new_instances.empty()) {
            auto print_object = new SLAPrintObject(this, &model_object);

            // FIXME: this invalidates the transformed mesh in SLAPrintObject
            // which is expensive to calculate (especially the raw_mesh() call)
            print_object->set_trafo(sla_trafo(*this, model_object), model_object.instances.front()->is_left_handed());

            print_object->set_instances(std::move(new_instances));

            SLAPrintObjectConfig new_config = m_default_object_config;
            normalize_and_apply_config(new_config, model_object.config);
            print_object->config_apply(new_config, true);
            print_objects_new.emplace_back(print_object);
            new_objects = true;
        }
    }

    if (m_objects != print_objects_new) {
        this->call_cancel_callback();
        update_apply_status(this->invalidate_all_steps());
        m_objects = print_objects_new;
        // Delete the PrintObjects marked as Unknown or Deleted.
        for (auto &pos : print_object_status)
            if (pos.status == PrintObjectStatus::Unknown || pos.status == PrintObjectStatus::Deleted) {
                update_apply_status(pos.print_object->invalidate_all_steps());
                delete pos.print_object;
            }
        if (new_objects)
            update_apply_status(false);
    }

    if(m_objects.empty()) {
        m_printer.release();
        m_printer_input.clear();
        m_print_statistics.clear();
    }

#ifdef _DEBUG
    check_model_ids_equal(m_model, model);
#endif /* _DEBUG */

    m_full_print_config = std::move(config);
    return static_cast<ApplyStatus>(apply_status);
}

// After calling the apply() function, set_task() may be called to limit the task to be processed by process().
void SLAPrint::set_task(const TaskParams &params)
{
    // Grab the lock for the Print / PrintObject milestones.
    tbb::mutex::scoped_lock lock(this->state_mutex());

    int n_object_steps = int(params.to_object_step) + 1;
    if (n_object_steps == 0)
        n_object_steps = int(slaposCount);

    if (params.single_model_object.valid()) {
        // Find the print object to be processed with priority.
        SLAPrintObject *print_object = nullptr;
        size_t          idx_print_object = 0;
        for (; idx_print_object < m_objects.size(); ++ idx_print_object)
            if (m_objects[idx_print_object]->model_object()->id() == params.single_model_object) {
                print_object = m_objects[idx_print_object];
                break;
            }
        assert(print_object != nullptr);
        // Find out whether the priority print object is being currently processed.
        bool running = false;
        for (int istep = 0; istep < n_object_steps; ++ istep) {
            if (! print_object->m_stepmask[size_t(istep)])
                // Step was skipped, cancel.
                break;
            if (print_object->is_step_started_unguarded(SLAPrintObjectStep(istep))) {
                // No step was skipped, and a wanted step is being processed. Don't cancel.
                running = true;
                break;
            }
        }
        if (! running)
            this->call_cancel_callback();

        // Now the background process is either stopped, or it is inside one of the print object steps to be calculated anyway.
        if (params.single_model_instance_only) {
            // Suppress all the steps of other instances.
            for (SLAPrintObject *po : m_objects)
                for (size_t istep = 0; istep < slaposCount; ++ istep)
                    po->m_stepmask[istep] = false;
        } else if (! running) {
            // Swap the print objects, so that the selected print_object is first in the row.
            // At this point the background processing must be stopped, so it is safe to shuffle print objects.
            if (idx_print_object != 0)
                std::swap(m_objects.front(), m_objects[idx_print_object]);
        }
        // and set the steps for the current object.
        for (int istep = 0; istep < n_object_steps; ++ istep)
            print_object->m_stepmask[size_t(istep)] = true;
        for (int istep = n_object_steps; istep < int(slaposCount); ++ istep)
            print_object->m_stepmask[size_t(istep)] = false;
    } else {
        // Slicing all objects.
        bool running = false;
        for (SLAPrintObject *print_object : m_objects)
            for (int istep = 0; istep < n_object_steps; ++ istep) {
                if (! print_object->m_stepmask[size_t(istep)]) {
                    // Step may have been skipped. Restart.
                    goto loop_end;
                }
                if (print_object->is_step_started_unguarded(SLAPrintObjectStep(istep))) {
                    // This step is running, and the state cannot be changed due to the this->state_mutex() being locked.
                    // It is safe to manipulate m_stepmask of other SLAPrintObjects and SLAPrint now.
                    running = true;
                    goto loop_end;
                }
            }
    loop_end:
        if (! running)
            this->call_cancel_callback();
        for (SLAPrintObject *po : m_objects) {
            for (int istep = 0; istep < n_object_steps; ++ istep)
                po->m_stepmask[size_t(istep)] = true;
            for (auto istep = size_t(n_object_steps); istep < slaposCount; ++ istep)
                po->m_stepmask[istep] = false;
        }
    }

    if (params.to_object_step != -1 || params.to_print_step != -1) {
        // Limit the print steps.
        size_t istep = (params.to_object_step != -1) ? 0 : size_t(params.to_print_step) + 1;
        for (; istep < m_stepmask.size(); ++ istep)
            m_stepmask[istep] = false;
    }
}

// Clean up after process() finished, either with success, error or if canceled.
// The adjustments on the SLAPrint / SLAPrintObject data due to set_task() are to be reverted here.
void SLAPrint::finalize()
{
    for (SLAPrintObject *po : m_objects)
        for (size_t istep = 0; istep < slaposCount; ++ istep)
            po->m_stepmask[istep] = true;
    for (size_t istep = 0; istep < slapsCount; ++ istep)
        m_stepmask[istep] = true;
}

// Generate a recommended output file name based on the format template, default extension, and template parameters
// (timestamps, object placeholders derived from the model, current placeholder prameters and print statistics.
// Use the final print statistics if available, or just keep the print statistics placeholders if not available yet (before the output is finalized).
std::string SLAPrint::output_filename(const std::string &filename_base) const
{
    DynamicConfig config = this->finished() ? this->print_statistics().config() : this->print_statistics().placeholders();
    return this->PrintBase::output_filename(m_print_config.output_filename_format.value, ".sl1", filename_base, &config);
}

namespace {

bool is_zero_elevation(const SLAPrintObjectConfig &c) {
    bool en_implicit = c.support_object_elevation.getFloat() <= EPSILON &&
                       c.pad_enable.getBool() && c.supports_enable.getBool();
    bool en_explicit = c.pad_zero_elevation.getBool() &&
                       c.supports_enable.getBool();

    return en_implicit || en_explicit;
}

// Compile the argument for support creation from the static print config.
sla::SupportConfig make_support_cfg(const SLAPrintObjectConfig& c) {
    sla::SupportConfig scfg;

    scfg.head_front_radius_mm = 0.5*c.support_head_front_diameter.getFloat();
    scfg.head_back_radius_mm = 0.5*c.support_pillar_diameter.getFloat();
    scfg.head_penetration_mm = c.support_head_penetration.getFloat();
    scfg.head_width_mm = c.support_head_width.getFloat();
    scfg.object_elevation_mm = is_zero_elevation(c) ?
                                   0. : c.support_object_elevation.getFloat();
    scfg.bridge_slope = c.support_critical_angle.getFloat() * PI / 180.0 ;
    scfg.max_bridge_length_mm = c.support_max_bridge_length.getFloat();
    scfg.max_pillar_link_distance_mm = c.support_max_pillar_link_distance.getFloat();
    switch(c.support_pillar_connection_mode.getInt()) {
    case slapcmZigZag:
        scfg.pillar_connection_mode = sla::PillarConnectionMode::zigzag; break;
    case slapcmCross:
        scfg.pillar_connection_mode = sla::PillarConnectionMode::cross; break;
    case slapcmDynamic:
        scfg.pillar_connection_mode = sla::PillarConnectionMode::dynamic; break;
    }
    scfg.ground_facing_only = c.support_buildplate_only.getBool();
    scfg.pillar_widening_factor = c.support_pillar_widening_factor.getFloat();
    scfg.base_radius_mm = 0.5*c.support_base_diameter.getFloat();
    scfg.base_height_mm = c.support_base_height.getFloat();
    scfg.pillar_base_safety_distance_mm =
        c.support_base_safety_distance.getFloat() < EPSILON ?
        scfg.safety_distance_mm : c.support_base_safety_distance.getFloat();

    return scfg;
}

sla::PoolConfig::EmbedObject builtin_pad_cfg(const SLAPrintObjectConfig& c) {
    sla::PoolConfig::EmbedObject ret;

    ret.enabled = is_zero_elevation(c);

    if(ret.enabled) {
        ret.object_gap_mm        = c.pad_object_gap.getFloat();
        ret.stick_width_mm       = c.pad_object_connector_width.getFloat();
        ret.stick_stride_mm      = c.pad_object_connector_stride.getFloat();
        ret.stick_penetration_mm = c.pad_object_connector_penetration
                                    .getFloat();
    }

    return ret;
}

sla::PoolConfig make_pool_config(const SLAPrintObjectConfig& c) {
    sla::PoolConfig pcfg;

    pcfg.min_wall_thickness_mm = c.pad_wall_thickness.getFloat();
    pcfg.wall_slope = c.pad_wall_slope.getFloat() * PI / 180.0;

    // We do not support radius for now
    pcfg.edge_radius_mm = 0.0; //c.pad_edge_radius.getFloat();

    pcfg.max_merge_distance_mm = c.pad_max_merge_distance.getFloat();
    pcfg.min_wall_height_mm = c.pad_wall_height.getFloat();

    // set builtin pad implicitly ON
    pcfg.embed_object = builtin_pad_cfg(c);

    return pcfg;
}

}

std::string SLAPrint::validate() const
{
    for(SLAPrintObject * po : m_objects) {

        const ModelObject *mo = po->model_object();
        bool supports_en = po->config().supports_enable.getBool();

        if(supports_en &&
           mo->sla_points_status == sla::PointsStatus::UserModified &&
           mo->sla_support_points.empty())
            return L("Cannot proceed without support points! "
                     "Add support points or disable support generation.");

        sla::SupportConfig cfg = make_support_cfg(po->config());

        double pinhead_width =
                2 * cfg.head_front_radius_mm +
                cfg.head_width_mm +
                2 * cfg.head_back_radius_mm -
                cfg.head_penetration_mm;

        double elv = cfg.object_elevation_mm;

        if(supports_en && elv > EPSILON && elv < pinhead_width )
            return L(
                "Elevation is too low for object. Use the \"Pad around "
                "obect\" feature to print the object without elevation.");

        sla::PoolConfig::EmbedObject builtinpad = builtin_pad_cfg(po->config());
        if(supports_en && builtinpad.enabled &&
           cfg.pillar_base_safety_distance_mm < builtinpad.object_gap_mm) {
            return L(
                "The endings of the support pillars will be deployed on the "
                "gap between the object and the pad. 'Support base safety "
                "distance' has to be greater than the 'Pad object gap' "
                "parameter to avoid this.");
        }
    }

    return "";
}

bool SLAPrint::invalidate_step(SLAPrintStep step)
{
    bool invalidated = Inherited::invalidate_step(step);

    // propagate to dependent steps
    if (step == slapsMergeSlicesAndEval) {
        invalidated |= this->invalidate_all_steps();
    }

    return invalidated;
}

void SLAPrint::process()
{
    using namespace sla;
    using ExPolygon = Slic3r::ExPolygon;

    if(m_objects.empty()) return;

    // Assumption: at this point the print objects should be populated only with
    // the model objects we have to process and the instances are also filtered

    // shortcut to initial layer height
    double ilhd = m_material_config.initial_layer_height.getFloat();
    auto   ilh  = float(ilhd);

    coord_t      ilhs     = scaled(ilhd);
    const size_t objcount = m_objects.size();

    static const unsigned min_objstatus = 0;   // where the per object operations start
    static const unsigned max_objstatus = 50;  // where the per object operations end

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
    auto slice_model = [this, ilhs, ilh](SLAPrintObject& po) {
        const TriangleMesh& mesh = po.transformed_mesh();

        // We need to prepare the slice index...

        double  lhd  = m_objects.front()->m_config.layer_height.getFloat();
        float   lh   = float(lhd);
        coord_t lhs  = scaled(lhd);
        auto && bb3d = mesh.bounding_box();
        double  minZ = bb3d.min(Z) - po.get_elevation();
        double  maxZ = bb3d.max(Z);
        auto    minZf = float(minZ);
        coord_t minZs = scaled(minZ);
        coord_t maxZs = scaled(maxZ);

        po.m_slice_index.clear();

        size_t cap = size_t(1 + (maxZs - minZs - ilhs) / lhs);
        po.m_slice_index.reserve(cap);

        po.m_slice_index.emplace_back(minZs + ilhs, minZf + ilh / 2.f, ilh);

        for(coord_t h = minZs + ilhs + lhs; h <= maxZs; h += lhs)
            po.m_slice_index.emplace_back(h, unscaled<float>(h) - lh / 2.f, lh);

        // Just get the first record that is form the model:
        auto slindex_it =
                po.closest_slice_record(po.m_slice_index, float(bb3d.min(Z)));

        if(slindex_it == po.m_slice_index.end())
            //TRN To be shown at the status bar on SLA slicing error.
            throw std::runtime_error(
                L("Slicing had to be stopped due to an internal error: "
                  "Inconsistent slice index."));

        po.m_model_height_levels.clear();
        po.m_model_height_levels.reserve(po.m_slice_index.size());
        for(auto it = slindex_it; it != po.m_slice_index.end(); ++it)
            po.m_model_height_levels.emplace_back(it->slice_level());

        TriangleMeshSlicer slicer(&mesh);

        po.m_model_slices.clear();
        slicer.slice(po.m_model_height_levels,
                     float(po.config().slice_closing_radius.value),
                     &po.m_model_slices,
                     [this](){ throw_if_canceled(); });

        auto mit = slindex_it;
        double doffs = m_printer_config.absolute_correction.getFloat();
        coord_t clpr_offs = scaled(doffs);
        for(size_t id = 0;
            id < po.m_model_slices.size() && mit != po.m_slice_index.end();
            id++)
        {
            // We apply the printer correction offset here.
            if(clpr_offs != 0)
                po.m_model_slices[id] =
                        offset_ex(po.m_model_slices[id], float(clpr_offs));

            mit->set_model_slice_idx(po, id); ++mit;
        }

        if(po.m_config.supports_enable.getBool() ||
           po.m_config.pad_enable.getBool())
        {
            po.m_supportdata.reset(
                new SLAPrintObject::SupportData(po.transformed_mesh()) );
        }
    };

    // In this step we check the slices, identify island and cover them with
    // support points. Then we sprinkle the rest of the mesh.
    auto support_points = [this, ostepd](SLAPrintObject& po) {
        // If supports are disabled, we can skip the model scan.
        if(!po.m_config.supports_enable.getBool()) return;

        if (!po.m_supportdata)
            po.m_supportdata.reset(
                new SLAPrintObject::SupportData(po.transformed_mesh()));

        const ModelObject& mo = *po.m_model_object;

        BOOST_LOG_TRIVIAL(debug) << "Support point count "
                                 << mo.sla_support_points.size();

        // Unless the user modified the points or we already did the calculation, we will do
        // the autoplacement. Otherwise we will just blindly copy the frontend data
        // into the backend cache.
        if (mo.sla_points_status != sla::PointsStatus::UserModified) {

            // Hypothetical use of the slice index:
            // auto bb = po.transformed_mesh().bounding_box();
            // auto range = po.get_slice_records(bb.min(Z));
            // std::vector<float> heights; heights.reserve(range.size());
            // for(auto& record : range) heights.emplace_back(record.slice_level());

            // calculate heights of slices (slices are calculated already)
            const std::vector<float>& heights = po.m_model_height_levels;

            this->throw_if_canceled();
            SLAAutoSupports::Config config;
            const SLAPrintObjectConfig& cfg = po.config();

            // the density config value is in percents:
            config.density_relative = float(cfg.support_points_density_relative / 100.f);
            config.minimal_distance = float(cfg.support_points_minimal_distance);
            config.head_diameter    = float(cfg.support_head_front_diameter);

            // scaling for the sub operations
            double d = ostepd * OBJ_STEP_LEVELS[slaposSupportPoints] / 100.0;
            double init = m_report_status.status();

            auto statuscb = [this, d, init](unsigned st)
            {
                double current = init + st * d;
                if(std::round(m_report_status.status()) < std::round(current))
                    m_report_status(*this, current,
                                    OBJ_STEP_LABELS(slaposSupportPoints));

            };

            // Construction of this object does the calculation.
            this->throw_if_canceled();
            SLAAutoSupports auto_supports(po.transformed_mesh(),
                                          po.m_supportdata->emesh,
                                          po.get_model_slices(),
                                          heights,
                                          config,
                                          [this]() { throw_if_canceled(); },
                                          statuscb);

            // Now let's extract the result.
            const std::vector<sla::SupportPoint>& points = auto_supports.output();
            this->throw_if_canceled();
            po.m_supportdata->support_points = points;

            BOOST_LOG_TRIVIAL(debug) << "Automatic support points: "
                                     << po.m_supportdata->support_points.size();

            // Using RELOAD_SLA_SUPPORT_POINTS to tell the Plater to pass
            // the update status to GLGizmoSlaSupports
            m_report_status(*this,
                            -1,
                            L("Generating support points"),
                            SlicingStatus::RELOAD_SLA_SUPPORT_POINTS);
        }
        else {
            // There are either some points on the front-end, or the user
            // removed them on purpose. No calculation will be done.
            po.m_supportdata->support_points = po.transformed_support_points();
        }

        // If the zero elevation mode is engaged, we have to filter out all the
        // points that are on the bottom of the object
        if (is_zero_elevation(po.config())) {
            double gnd       = po.m_supportdata->emesh.ground_level();
            auto & pts       = po.m_supportdata->support_points;
            double tolerance = po.config().pad_enable.getBool()
                                   ? po.m_config.pad_wall_thickness.getFloat()
                                   : po.m_config.support_base_height.getFloat();

            // get iterator to the reorganized vector end
            auto endit = std::remove_if(
                pts.begin(),
                pts.end(),
                [tolerance, gnd](const sla::SupportPoint &sp) {
                    double diff = std::abs(gnd - double(sp.pos(Z)));
                    return diff <= tolerance;
                });

            // erase all elements after the new end
            pts.erase(endit, pts.end());
        }
    };

    // In this step we create the supports
    auto support_tree = [this, ostepd](SLAPrintObject& po)
    {
        if(!po.m_supportdata) return;

        sla::PoolConfig pcfg = make_pool_config(po.m_config);

        if (pcfg.embed_object)
            po.m_supportdata->emesh.ground_level_offset(
                pcfg.min_wall_thickness_mm);

        if(!po.m_config.supports_enable.getBool()) {

            // Generate empty support tree. It can still host a pad
            po.m_supportdata->support_tree_ptr.reset(
                    new SLASupportTree(po.m_supportdata->emesh.ground_level()));

            return;
        }

        sla::SupportConfig scfg = make_support_cfg(po.m_config);
        sla::Controller ctl;

        // scaling for the sub operations
        double d = ostepd * OBJ_STEP_LEVELS[slaposSupportTree] / 100.0;
        double init = m_report_status.status();

        ctl.statuscb = [this, d, init](unsigned st, const std::string &logmsg)
        {
            double current = init + st * d;
            if(std::round(m_report_status.status()) < std::round(current))
                m_report_status(*this, current,
                                OBJ_STEP_LABELS(slaposSupportTree),
                                SlicingStatus::DEFAULT,
                                logmsg);

        };

        ctl.stopcondition = [this](){ return canceled(); };
        ctl.cancelfn = [this]() { throw_if_canceled(); };

        po.m_supportdata->support_tree_ptr.reset(
                    new SLASupportTree(po.m_supportdata->support_points,
                                       po.m_supportdata->emesh, scfg, ctl));

        throw_if_canceled();

        // Create the unified mesh
        auto rc = SlicingStatus::RELOAD_SCENE;

        // This is to prevent "Done." being displayed during merged_mesh()
        m_report_status(*this, -1, L("Visualizing supports"));
        po.m_supportdata->support_tree_ptr->merged_mesh();

        BOOST_LOG_TRIVIAL(debug) << "Processed support point count "
                                 << po.m_supportdata->support_points.size();

        // Check the mesh for later troubleshooting.
        if(po.support_mesh().empty())
            BOOST_LOG_TRIVIAL(warning) << "Support mesh is empty";

        m_report_status(*this, -1, L("Visualizing supports"), rc);
    };

    // This step generates the sla base pad
    auto base_pool = [this](SLAPrintObject& po) {
        // this step can only go after the support tree has been created
        // and before the supports had been sliced. (or the slicing has to be
        // repeated)

        if(po.m_config.pad_enable.getBool())
        {
            // Get the distilled pad configuration from the config
            sla::PoolConfig pcfg = make_pool_config(po.m_config);

            ExPolygons bp; // This will store the base plate of the pad.
            double   pad_h             = sla::get_pad_fullheight(pcfg);
            const TriangleMesh &trmesh = po.transformed_mesh();

            // This call can get pretty time consuming
            auto thrfn = [this](){ throw_if_canceled(); };

            if (!po.m_config.supports_enable.getBool() || pcfg.embed_object) {
                // No support (thus no elevation) or zero elevation mode
                // we sometimes call it "builtin pad" is enabled so we will
                // get a sample from the bottom of the mesh and use it for pad
                // creation.
                sla::base_plate(trmesh,
                                bp,
                                float(pad_h),
                                float(po.m_config.layer_height.getFloat()),
                                thrfn);
            }

            pcfg.throw_on_cancel = thrfn;
            po.m_supportdata->support_tree_ptr->add_pad(bp, pcfg);
        } else if(po.m_supportdata && po.m_supportdata->support_tree_ptr) {
            po.m_supportdata->support_tree_ptr->remove_pad();
        }

        po.throw_if_canceled();
        auto rc = SlicingStatus::RELOAD_SCENE;
        m_report_status(*this, -1, L("Visualizing supports"), rc);
    };

    // Slicing the support geometries similarly to the model slicing procedure.
    // If the pad had been added previously (see step "base_pool" than it will
    // be part of the slices)
    auto slice_supports = [this](SLAPrintObject& po) {
        auto& sd = po.m_supportdata;

        if(sd) sd->support_slices.clear();

        // Don't bother if no supports and no pad is present.
        if (!po.m_config.supports_enable.getBool() &&
            !po.m_config.pad_enable.getBool())
            return;

        if(sd && sd->support_tree_ptr) {

            std::vector<float> heights; heights.reserve(po.m_slice_index.size());

            for(auto& rec : po.m_slice_index) {
                heights.emplace_back(rec.slice_level());
            }

            sd->support_slices = sd->support_tree_ptr->slice(
                        heights, float(po.config().slice_closing_radius.value));
        }

        double doffs = m_printer_config.absolute_correction.getFloat();
        coord_t clpr_offs = scaled(doffs);
        for(size_t i = 0;
            i < sd->support_slices.size() && i < po.m_slice_index.size();
            ++i)
        {
            // We apply the printer correction offset here.
            if(clpr_offs != 0)
                sd->support_slices[i] =
                    offset_ex(sd->support_slices[i], float(clpr_offs));

            po.m_slice_index[i].set_support_slice_idx(po, i);
        }

        // Using RELOAD_SLA_PREVIEW to tell the Plater to pass the update
        // status to the 3D preview to load the SLA slices.
        m_report_status(*this, -2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
    };

    // Merging the slices from all the print objects into one slice grid and
    // calculating print statistics from the merge result.
    auto merge_slices_and_eval_stats = [this, ilhs]() {

        // clear the rasterizer input
        m_printer_input.clear();

        size_t mx = 0;
        for(SLAPrintObject * o : m_objects) {
            if(auto m = o->get_slice_index().size() > mx) mx = m;
        }

        m_printer_input.reserve(mx);

        auto eps = coord_t(SCALED_EPSILON);

        for(SLAPrintObject * o : m_objects) {
            coord_t gndlvl = o->get_slice_index().front().print_level() - ilhs;

            for(const SliceRecord& slicerecord : o->get_slice_index()) {
                coord_t lvlid = slicerecord.print_level() - gndlvl;

                // Neat trick to round the layer levels to the grid.
                lvlid = eps * (lvlid / eps);

                auto it = std::lower_bound(m_printer_input.begin(),
                                           m_printer_input.end(),
                                           PrintLayer(lvlid));

                if(it == m_printer_input.end() || it->level() != lvlid)
                    it = m_printer_input.insert(it, PrintLayer(lvlid));


                it->add(slicerecord);
            }
        }

        m_print_statistics.clear();

        using ClipperPoint  = ClipperLib::IntPoint;
        using ClipperPolygon = ClipperLib::Polygon; // see clipper_polygon.hpp in libnest2d
        using ClipperPolygons = std::vector<ClipperPolygon>;
        namespace sl = libnest2d::shapelike;    // For algorithms

        // Set up custom union and diff functions for clipper polygons
        auto polyunion = [] (const ClipperPolygons& subjects)
        {
            ClipperLib::Clipper clipper;

            bool closed = true;

            for(auto& path : subjects) {
                clipper.AddPath(path.Contour, ClipperLib::ptSubject, closed);
                clipper.AddPaths(path.Holes, ClipperLib::ptSubject, closed);
            }

            auto mode = ClipperLib::pftPositive;

            return libnest2d::clipper_execute(clipper, ClipperLib::ctUnion, mode, mode);
        };

        auto polydiff = [](const ClipperPolygons& subjects, const ClipperPolygons& clips)
        {
            ClipperLib::Clipper clipper;

            bool closed = true;

            for(auto& path : subjects) {
                clipper.AddPath(path.Contour, ClipperLib::ptSubject, closed);
                clipper.AddPaths(path.Holes, ClipperLib::ptSubject, closed);
            }

            for(auto& path : clips) {
                clipper.AddPath(path.Contour, ClipperLib::ptClip, closed);
                clipper.AddPaths(path.Holes, ClipperLib::ptClip, closed);
            }

            auto mode = ClipperLib::pftPositive;

            return libnest2d::clipper_execute(clipper, ClipperLib::ctDifference, mode, mode);
        };

        // libnest calculates positive area for clockwise polygons, Slic3r is in counter-clockwise
        auto areafn = [](const ClipperPolygon& poly) { return - sl::area(poly); };

        const double area_fill          = m_printer_config.area_fill.getFloat()*0.01;// 0.5 (50%);
        const double fast_tilt          = m_printer_config.fast_tilt_time.getFloat();// 5.0;
        const double slow_tilt          = m_printer_config.slow_tilt_time.getFloat();// 8.0;

        const double init_exp_time      = m_material_config.initial_exposure_time.getFloat();
        const double exp_time           = m_material_config.exposure_time.getFloat();

        const int    fade_layers_cnt    = m_default_object_config.faded_layers.getInt();// 10 // [3;20]

        const auto width                = scaled<double>(m_printer_config.display_width.getFloat());
        const auto height               = scaled<double>(m_printer_config.display_height.getFloat());
        const double display_area       = width*height;

        // get polygons for all instances in the object
        auto get_all_polygons =
                [](const ExPolygons& input_polygons,
                   const std::vector<SLAPrintObject::Instance>& instances,
                   bool is_lefthanded)
        {
            ClipperPolygons polygons;
            polygons.reserve(input_polygons.size() * instances.size());

            for (const ExPolygon& polygon : input_polygons) {
                if(polygon.contour.empty()) continue;

                for (size_t i = 0; i < instances.size(); ++i)
                {
                    ClipperPolygon poly;

                    // We need to reverse if flpXY OR is_lefthanded is true but
                    // not if both are true which is a logical inequality (XOR)
                    bool needreverse = /*flpXY !=*/ is_lefthanded;

                    // should be a move
                    poly.Contour.reserve(polygon.contour.size() + 1);

                    auto& cntr = polygon.contour.points;
                    if(needreverse)
                        for(auto it = cntr.rbegin(); it != cntr.rend(); ++it)
                            poly.Contour.emplace_back(it->x(), it->y());
                    else
                        for(auto& p : cntr)
                            poly.Contour.emplace_back(p.x(), p.y());

                    for(auto& h : polygon.holes) {
                        poly.Holes.emplace_back();
                        auto& hole = poly.Holes.back();
                        hole.reserve(h.points.size() + 1);

                        if(needreverse)
                            for(auto it = h.points.rbegin(); it != h.points.rend(); ++it)
                                hole.emplace_back(it->x(), it->y());
                        else
                            for(auto& p : h.points)
                                hole.emplace_back(p.x(), p.y());
                    }

                    if(is_lefthanded) {
                        for(auto& p : poly.Contour) p.X = -p.X;
                        for(auto& h : poly.Holes) for(auto& p : h) p.X = -p.X;
                    }

                    sl::rotate(poly, double(instances[i].rotation));
                    sl::translate(poly, ClipperPoint{instances[i].shift(X),
                                                     instances[i].shift(Y)});

                    polygons.emplace_back(std::move(poly));
                }
            }
            return polygons;
        };

        double supports_volume(0.0);
        double models_volume(0.0);

        double estim_time(0.0);

        size_t slow_layers = 0;
        size_t fast_layers = 0;

        const double delta_fade_time = (init_exp_time - exp_time) / (fade_layers_cnt + 1);
        double fade_layer_time = init_exp_time;

        SpinMutex mutex;
        using Lock = std::lock_guard<SpinMutex>;

        // Going to parallel:
        auto printlayerfn = [this,
                // functions and read only vars
                get_all_polygons, polyunion, polydiff, areafn,
                area_fill, display_area, exp_time, init_exp_time, fast_tilt, slow_tilt, delta_fade_time,

                // write vars
                &mutex, &models_volume, &supports_volume, &estim_time, &slow_layers,
                &fast_layers, &fade_layer_time](size_t sliced_layer_cnt)
        {
            PrintLayer& layer = m_printer_input[sliced_layer_cnt];

            // vector of slice record references
            auto& slicerecord_references = layer.slices();

            if(slicerecord_references.empty()) return;

            // Layer height should match for all object slices for a given level.
            const auto l_height = double(slicerecord_references.front().get().layer_height());

            // Calculation of the consumed material

            ClipperPolygons model_polygons;
            ClipperPolygons supports_polygons;

            size_t c = std::accumulate(layer.slices().begin(),
                                       layer.slices().end(),
                                       size_t(0),
                                       [](size_t a, const SliceRecord &sr) {
                                           return a + sr.get_slice(soModel)
                                                        .size();
                                       });

            model_polygons.reserve(c);

            c = std::accumulate(layer.slices().begin(),
                                layer.slices().end(),
                                size_t(0),
                                [](size_t a, const SliceRecord &sr) {
                                    return a + sr.get_slice(soModel).size();
                                });

            supports_polygons.reserve(c);

            for(const SliceRecord& record : layer.slices()) {
                const SLAPrintObject *po = record.print_obj();

                const ExPolygons &modelslices = record.get_slice(soModel);

                bool is_lefth = record.print_obj()->is_left_handed();
                if (!modelslices.empty()) {
                    ClipperPolygons v = get_all_polygons(modelslices, po->instances(), is_lefth);
                    for(ClipperPolygon& p_tmp : v) model_polygons.emplace_back(std::move(p_tmp));
                }

                const ExPolygons &supportslices = record.get_slice(soSupport);

                if (!supportslices.empty()) {
                    ClipperPolygons v = get_all_polygons(supportslices, po->instances(), is_lefth);
                    for(ClipperPolygon& p_tmp : v) supports_polygons.emplace_back(std::move(p_tmp));
                }
            }

            model_polygons = polyunion(model_polygons);
            double layer_model_area = 0;
            for (const ClipperPolygon& polygon : model_polygons)
                layer_model_area += areafn(polygon);

            if (layer_model_area < 0 || layer_model_area > 0) {
                Lock lck(mutex); models_volume += layer_model_area * l_height;
            }

            if(!supports_polygons.empty()) {
                if(model_polygons.empty()) supports_polygons = polyunion(supports_polygons);
                else supports_polygons = polydiff(supports_polygons, model_polygons);
                // allegedly, union of subject is done withing the diff according to the pftPositive polyFillType
            }

            double layer_support_area = 0;
            for (const ClipperPolygon& polygon : supports_polygons)
                layer_support_area += areafn(polygon);

            if (layer_support_area < 0 || layer_support_area > 0) {
                Lock lck(mutex); supports_volume += layer_support_area * l_height;
            }

            // Here we can save the expensively calculated polygons for printing
            ClipperPolygons trslices;
            trslices.reserve(model_polygons.size() + supports_polygons.size());
            for(ClipperPolygon& poly : model_polygons) trslices.emplace_back(std::move(poly));
            for(ClipperPolygon& poly : supports_polygons) trslices.emplace_back(std::move(poly));

            layer.transformed_slices(polyunion(trslices));

            // Calculation of the slow and fast layers to the future controlling those values on FW

            const bool is_fast_layer = (layer_model_area + layer_support_area) <= display_area*area_fill;
            const double tilt_time = is_fast_layer ? fast_tilt : slow_tilt;

            { Lock lck(mutex);
                if (is_fast_layer)
                    fast_layers++;
                else
                    slow_layers++;


                // Calculation of the printing time

                if (sliced_layer_cnt < 3)
                    estim_time += init_exp_time;
                else if (fade_layer_time > exp_time)
                {
                    fade_layer_time -= delta_fade_time;
                    estim_time += fade_layer_time;
                }
                else
                    estim_time += exp_time;

                estim_time += tilt_time;
            }
        };

        // sequential version for debugging:
        // for(size_t i = 0; i < m_printer_input.size(); ++i) printlayerfn(i);
        tbb::parallel_for<size_t, decltype(printlayerfn)>(0, m_printer_input.size(), printlayerfn);

        auto SCALING2 = SCALING_FACTOR * SCALING_FACTOR;
        m_print_statistics.support_used_material = supports_volume * SCALING2;
        m_print_statistics.objects_used_material = models_volume  * SCALING2;

        // Estimated printing time
        // A layers count o the highest object
        if (m_printer_input.size() == 0)
            m_print_statistics.estimated_print_time = "N/A";
        else
            m_print_statistics.estimated_print_time = get_time_dhms(float(estim_time));

        m_print_statistics.fast_layers_count = fast_layers;
        m_print_statistics.slow_layers_count = slow_layers;

        m_report_status(*this, -2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
    };

    // Rasterizing the model objects, and their supports
    auto rasterize = [this]() {
        if(canceled()) return;

        { // create a raster printer for the current print parameters
            double layerh = m_default_object_config.layer_height.getFloat();
            m_printer.reset(new SLAPrinter(m_printer_config,
                                           m_material_config,
                                           layerh));
        }

        // Allocate space for all the layers
        SLAPrinter& printer = *m_printer;
        auto lvlcnt = unsigned(m_printer_input.size());
        printer.layers(lvlcnt);

        // coefficient to map the rasterization state (0-99) to the allocated
        // portion (slot) of the process state
        double sd = (100 - max_objstatus) / 100.0;

        // slot is the portion of 100% that is realted to rasterization
        unsigned slot = PRINT_STEP_LEVELS[slapsRasterize];

        // pst: previous state
        double pst = m_report_status.status();

        double increment = (slot * sd) / m_printer_input.size();
        double dstatus = m_report_status.status();

        SpinMutex slck;

        // procedure to process one height level. This will run in parallel
        auto lvlfn =
        [this, &slck, &printer, increment, &dstatus, &pst]
            (unsigned level_id)
        {
            if(canceled()) return;

            PrintLayer& printlayer = m_printer_input[level_id];

            // Switch to the appropriate layer in the printer
            printer.begin_layer(level_id);

            for(const ClipperLib::Polygon& poly : printlayer.transformed_slices())
                printer.draw_polygon(poly, level_id);

            // Finish the layer for later saving it.
            printer.finish_layer(level_id);

            // Status indication guarded with the spinlock
            {
                std::lock_guard<SpinMutex> lck(slck);
                dstatus += increment;
                double st = std::round(dstatus);
                if(st > pst) {
                    m_report_status(*this, st,
                                    PRINT_STEP_LABELS(slapsRasterize));
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

        // Set statistics values to the printer
        m_printer->set_statistics(
            {(m_print_statistics.objects_used_material
              + m_print_statistics.support_used_material) / 1000,
             double(m_default_object_config.faded_layers.getInt()),
             double(m_print_statistics.slow_layers_count),
             double(m_print_statistics.fast_layers_count)});
    };

    using slaposFn = std::function<void(SLAPrintObject&)>;
    using slapsFn  = std::function<void(void)>;

    std::array<slaposFn, slaposCount> pobj_program =
    {
        slice_model,
        support_points,
        support_tree,
        base_pool,
        slice_supports
    };

    std::array<slapsFn, slapsCount> print_program =
    {
        merge_slices_and_eval_stats,
        rasterize
    };

    double st = min_objstatus;
    unsigned incr = 0;

    BOOST_LOG_TRIVIAL(info) << "Start slicing process.";

    // TODO: this loop could run in parallel but should not exhaust all the CPU
    // power available
    // Calculate the support structures first before slicing the supports,
    // so that the preview will get displayed ASAP for all objects.
    std::vector<SLAPrintObjectStep> step_ranges = {slaposObjectSlice,
                                                   slaposSliceSupports,
                                                   slaposCount};

    for (size_t idx_range = 0; idx_range + 1 < step_ranges.size(); ++idx_range) {
        for (SLAPrintObject *po : m_objects) {

            BOOST_LOG_TRIVIAL(info)
                << "Slicing object " << po->model_object()->name;

            for (int s = int(step_ranges[idx_range]);
                 s < int(step_ranges[idx_range + 1]);
                 ++s) {
                auto currentstep = static_cast<SLAPrintObjectStep>(s);

                // Cancellation checking. Each step will check for
                // cancellation on its own and return earlier gracefully.
                // Just after it returns execution gets to this point and
                // throws the canceled signal.
                throw_if_canceled();

                st += incr * ostepd;

                if (po->m_stepmask[currentstep]
                    && po->set_started(currentstep)) {
                    m_report_status(*this,
                                    st,
                                    OBJ_STEP_LABELS(currentstep));
                    pobj_program[currentstep](*po);
                    throw_if_canceled();
                    po->set_done(currentstep);
                }

                incr = OBJ_STEP_LEVELS[currentstep];
            }
        }
    }

    std::array<SLAPrintStep, slapsCount> printsteps = {
        slapsMergeSlicesAndEval, slapsRasterize
    };

    // this would disable the rasterization step
    // m_stepmask[slapsRasterize] = false;

    double pstd = (100 - max_objstatus) / 100.0;
    st = max_objstatus;
    for(size_t s = 0; s < print_program.size(); ++s) {
        auto currentstep = printsteps[s];

        throw_if_canceled();

        if(m_stepmask[currentstep] && set_started(currentstep))
        {
            m_report_status(*this, st, PRINT_STEP_LABELS(currentstep));
            print_program[currentstep]();
            throw_if_canceled();
            set_done(currentstep);
        }

        st += PRINT_STEP_LEVELS[currentstep] * pstd;
    }

    // If everything vent well
    m_report_status(*this, 100, L("Slicing done"));
}

bool SLAPrint::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys, bool &invalidate_all_model_objects)
{
    if (opt_keys.empty())
        return false;

    static std::unordered_set<std::string> steps_full = {
        "initial_layer_height",
        "material_correction",
        "relative_correction",
        "absolute_correction",
        "gamma_correction"
    };

    // Cache the plenty of parameters, which influence the final rasterization only,
    // or they are only notes not influencing the rasterization step.
    static std::unordered_set<std::string> steps_rasterize = {
        "exposure_time",
        "initial_exposure_time",
        "display_width",
        "display_height",
        "display_pixels_x",
        "display_pixels_y",
        "display_mirror_x",
        "display_mirror_y",
        "display_orientation"
    };

    static std::unordered_set<std::string> steps_ignore = {
        "bed_shape",
        "max_print_height",
        "printer_technology",
        "output_filename_format",
        "fast_tilt_time",
        "slow_tilt_time",
        "area_fill"
    };

    std::vector<SLAPrintStep> steps;
    std::vector<SLAPrintObjectStep> osteps;
    bool invalidated = false;

    for (const t_config_option_key &opt_key : opt_keys) {
        if (steps_rasterize.find(opt_key) != steps_rasterize.end()) {
            // These options only affect the final rasterization, or they are just notes without influence on the output,
            // so there is nothing to invalidate.
            steps.emplace_back(slapsMergeSlicesAndEval);
        } else if (steps_ignore.find(opt_key) != steps_ignore.end()) {
            // These steps have no influence on the output. Just ignore them.
        } else if (steps_full.find(opt_key) != steps_full.end()) {
            steps.emplace_back(slapsMergeSlicesAndEval);
            osteps.emplace_back(slaposObjectSlice);
            invalidate_all_model_objects = true;
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
        if (! object->is_step_done_unguarded(step))
            return false;
    return true;
}

SLAPrintObject::SLAPrintObject(SLAPrint *print, ModelObject *model_object)
    : Inherited(print, model_object)
    , m_stepmask(slaposCount, true)
    , m_transformed_rmesh([this](TriangleMesh &obj) {
        obj = m_model_object->raw_mesh();
        if (!obj.empty()) {
            obj.transform(m_trafo);
            obj.require_shared_vertices();
        }
    })
{}

SLAPrintObject::~SLAPrintObject() {}

// Called by SLAPrint::apply().
// This method only accepts SLAPrintObjectConfig option keys.
bool SLAPrintObject::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    std::vector<SLAPrintObjectStep> steps;
    bool invalidated = false;
    for (const t_config_option_key &opt_key : opt_keys) {
        if (   opt_key == "layer_height"
            || opt_key == "faded_layers"
            || opt_key == "pad_enable"
            || opt_key == "pad_wall_thickness"
            || opt_key == "supports_enable"
            || opt_key == "support_object_elevation"
            || opt_key == "pad_zero_elevation"
            || opt_key == "slice_closing_radius") {
            steps.emplace_back(slaposObjectSlice);
        } else if (

               opt_key == "support_points_density_relative"
            || opt_key == "support_points_minimal_distance") {
            steps.emplace_back(slaposSupportPoints);
        } else if (
               opt_key == "support_head_front_diameter"
            || opt_key == "support_head_penetration"
            || opt_key == "support_head_width"
            || opt_key == "support_pillar_diameter"
            || opt_key == "support_pillar_connection_mode"
            || opt_key == "support_buildplate_only"
            || opt_key == "support_base_diameter"
            || opt_key == "support_base_height"
            || opt_key == "support_critical_angle"
            || opt_key == "support_max_bridge_length"
            || opt_key == "support_max_pillar_link_distance"
            || opt_key == "support_base_safety_distance"
            ) {
            steps.emplace_back(slaposSupportTree);
        } else if (
               opt_key == "pad_wall_height"
            || opt_key == "pad_max_merge_distance"
            || opt_key == "pad_wall_slope"
            || opt_key == "pad_edge_radius"
            || opt_key == "pad_object_gap"
            || opt_key == "pad_object_connector_stride"
            || opt_key == "pad_object_connector_width"
            || opt_key == "pad_object_connector_penetration"
            ) {
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
    } else if (step == slaposSupportPoints) {
        invalidated |= this->invalidate_steps({ slaposSupportTree, slaposBasePool, slaposSliceSupports });
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    } else if (step == slaposSupportTree) {
        invalidated |= this->invalidate_steps({ slaposBasePool, slaposSliceSupports });
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    } else if (step == slaposBasePool) {
        invalidated |= this->invalidate_steps({slaposSliceSupports});
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    } else if (step == slaposSliceSupports) {
        invalidated |= m_print->invalidate_step(slapsMergeSlicesAndEval);
    }
    return invalidated;
}

bool SLAPrintObject::invalidate_all_steps()
{
    return Inherited::invalidate_all_steps() | m_print->invalidate_all_steps();
}

double SLAPrintObject::get_elevation() const {
    if (is_zero_elevation(m_config)) return 0.;

    bool en = m_config.supports_enable.getBool();

    double ret = en ? m_config.support_object_elevation.getFloat() : 0.;

    if(m_config.pad_enable.getBool()) {
        // Normally the elevation for the pad itself would be the thickness of
        // its walls but currently it is half of its thickness. Whatever it
        // will be in the future, we provide the config to the get_pad_elevation
        // method and we will have the correct value
        sla::PoolConfig pcfg = make_pool_config(m_config);
        if(!pcfg.embed_object) ret += sla::get_pad_elevation(pcfg);
    }

    return ret;
}

double SLAPrintObject::get_current_elevation() const
{
    if (is_zero_elevation(m_config)) return 0.;

    bool has_supports = is_step_done(slaposSupportTree);
    bool has_pad      = is_step_done(slaposBasePool);

    if(!has_supports && !has_pad)
        return 0;
    else if(has_supports && !has_pad) {
        return m_config.support_object_elevation.getFloat();
    }

    return get_elevation();
}

Vec3d SLAPrint::relative_correction() const
{
    Vec3d corr(1., 1., 1.);

    if(printer_config().relative_correction.values.size() >= 2) {
        corr(X) = printer_config().relative_correction.values[0];
        corr(Y) = printer_config().relative_correction.values[0];
        corr(Z) = printer_config().relative_correction.values.back();
    }

    if(material_config().material_correction.values.size() >= 2) {
        corr(X) *= material_config().material_correction.values[0];
        corr(Y) *= material_config().material_correction.values[0];
        corr(Z) *= material_config().material_correction.values.back();
    }

    return corr;
}

namespace { // dummy empty static containers for return values in some methods
const std::vector<ExPolygons> EMPTY_SLICES;
const TriangleMesh EMPTY_MESH;
const ExPolygons EMPTY_SLICE;
const std::vector<sla::SupportPoint> EMPTY_SUPPORT_POINTS;
}

const SliceRecord SliceRecord::EMPTY(0, std::nanf(""), 0.f);

const std::vector<sla::SupportPoint>& SLAPrintObject::get_support_points() const
{
    return m_supportdata? m_supportdata->support_points : EMPTY_SUPPORT_POINTS;
}

const std::vector<ExPolygons> &SLAPrintObject::get_support_slices() const
{
    // assert(is_step_done(slaposSliceSupports));
    if (!m_supportdata) return EMPTY_SLICES;
    return m_supportdata->support_slices;
}

const ExPolygons &SliceRecord::get_slice(SliceOrigin o) const
{
    size_t idx = o == soModel ? m_model_slices_idx :
                                m_support_slices_idx;

    if(m_po == nullptr) return EMPTY_SLICE;

    const std::vector<ExPolygons>& v = o == soModel? m_po->get_model_slices() :
                                                     m_po->get_support_slices();

    if(idx >= v.size()) return EMPTY_SLICE;

    return idx >= v.size() ? EMPTY_SLICE : v[idx];
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

std::vector<sla::SupportPoint> SLAPrintObject::transformed_support_points() const
{
    assert(m_model_object != nullptr);
    std::vector<sla::SupportPoint>& spts = m_model_object->sla_support_points;

    // this could be cached as well
    std::vector<sla::SupportPoint> ret;
    ret.reserve(spts.size());

    for(sla::SupportPoint& sp : spts) {
        Vec3f transformed_pos = trafo().cast<float>() * sp.pos;
        ret.emplace_back(transformed_pos, sp.head_front_radius, sp.is_new_island);
    }

    return ret;
}

DynamicConfig SLAPrintStatistics::config() const
{
    DynamicConfig config;
    const std::string print_time = Slic3r::short_time(this->estimated_print_time);
    config.set_key_value("print_time", new ConfigOptionString(print_time));
    config.set_key_value("objects_used_material", new ConfigOptionFloat(this->objects_used_material));
    config.set_key_value("support_used_material", new ConfigOptionFloat(this->support_used_material));
    config.set_key_value("total_cost", new ConfigOptionFloat(this->total_cost));
    config.set_key_value("total_weight", new ConfigOptionFloat(this->total_weight));
    return config;
}

DynamicConfig SLAPrintStatistics::placeholders()
{
    DynamicConfig config;
    for (const std::string &key : {
        "print_time", "total_cost", "total_weight",
        "objects_used_material", "support_used_material" })
        config.set_key_value(key, new ConfigOptionString(std::string("{") + key + "}"));

    return config;
}

std::string SLAPrintStatistics::finalize_output_path(const std::string &path_in) const
{
    std::string final_path;
    try {
        boost::filesystem::path path(path_in);
        DynamicConfig cfg = this->config();
        PlaceholderParser pp;
        std::string new_stem = pp.process(path.stem().string(), 0, &cfg);
        final_path = (path.parent_path() / (new_stem + path.extension().string())).string();
    }
    catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to apply the print statistics to the export file name: " << ex.what();
        final_path = path_in;
    }
    return final_path;
}

void SLAPrint::StatusReporter::operator()(SLAPrint &         p,
                                          double             st,
                                          const std::string &msg,
                                          unsigned           flags,
                                          const std::string &logmsg)
{
    m_st = st;
    BOOST_LOG_TRIVIAL(info)
        << st << "% " << msg << (logmsg.empty() ? "" : ": ") << logmsg
        << log_memory_info();

    p.set_status(int(std::round(st)), msg, flags);
}

} // namespace Slic3r
